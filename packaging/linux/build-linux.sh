#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
artifact_basename=FairyWriter-0.1.0-x86_64.AppImage

if [[ ${1:-} != --inside-container ]]; then
	if ! command -v docker >/dev/null 2>&1; then
		echo "Docker is required to build the x86_64 Linux artifact from macOS." >&2
		exit 1
	fi
	output_root=${FAIRYWRITER_LINUX_OUTPUT:-"${source_root}/release-linux-x86_64"}
	snesrecomp_root=${FAIRYWRITER_SNESRECOMP_ROOT:-"${source_root}/third_party/snesrecomp"}
	if [[ ! -f ${snesrecomp_root}/runner/src/snes/interp816.c ]]; then
		echo "Pinned snesrecomp checkout is required at ${snesrecomp_root}." >&2
		exit 1
	fi
	mkdir -p "${output_root}"
	docker build \
		--platform linux/amd64 \
		--tag fairywriter-linux-x86_64 \
		--file "${source_root}/packaging/linux/Dockerfile" \
		"${source_root}"
	docker run --rm \
		--platform linux/amd64 \
		--user "$(id -u):$(id -g)" \
		--volume "${source_root}:/src:ro" \
		--volume "${snesrecomp_root}:/snesrecomp:ro" \
		--volume "${output_root}:/out" \
		fairywriter-linux-x86_64 \
		/src/packaging/linux/build-linux.sh --inside-container
	exit 0
fi

if [[ $(uname -m) != x86_64 ]]; then
	echo "Linux packaging must run in an x86_64 container." >&2
	exit 1
fi

work_root=$(mktemp -d /tmp/fairywriter-linux.XXXXXX)
cleanup()
{
	rm -rf "${work_root}"
}
trap cleanup EXIT

build_root="${work_root}/build"
appdir="${work_root}/AppDir"
artifact="/out/${artifact_basename}"
export GOCACHE="${work_root}/go-cache"
mkdir -p "${GOCACHE}"

cmake -S /src -B "${build_root}" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="${QTDIR}" \
	-DFAIRYWRITER_DEVELOPMENT_UI=OFF \
	-DFAIRYWRITER_DEVELOPER_TOOLS=OFF \
	-DBUILD_TESTING=ON
cmake --build "${build_root}" --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir "${build_root}" --output-on-failure
DESTDIR="${appdir}" cmake --install "${build_root}" --prefix /usr

export QMAKE="${QTDIR}/bin/qmake"
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so
export OUTPUT="${artifact}"
linuxdeploy \
	--appdir "${appdir}" \
	--executable "${appdir}/usr/bin/fairywriter" \
	--desktop-file "${appdir}/usr/share/applications/fairywriter.desktop" \
	--icon-file "${appdir}/usr/share/icons/hicolor/256x256/apps/fairywriter.png" \
	--plugin qt

# linuxdeploy-plugin-qt's EXTRA_PLATFORM_PLUGINS accepts one plugin name in
# this pinned release. Copy the additional display backends explicitly so
# both X11 and Wayland are present in the shipped AppImage.
qt_plugin_dir=$("${QMAKE}" -query QT_INSTALL_PLUGINS)
platform_dir="${appdir}/usr/plugins/platforms"
mkdir -p "${platform_dir}"
for plugin in libqwayland-generic.so libqwayland-egl.so; do
	install -m 0755 "${qt_plugin_dir}/platforms/${plugin}" "${platform_dir}/${plugin}"
done
linuxdeploy --appdir "${appdir}" --output appimage

file "${artifact}"
test "$(od -An -tx1 -N4 "${artifact}" | tr -d ' \n')" = 7f454c46

audit_root="${work_root}/audit"
extract-appimage "${artifact}" "${audit_root}/squashfs-root"
QT_QPA_PLATFORM=offscreen "${audit_root}/squashfs-root/AppRun" --version
# The production executable is the embedded SNES player, so the old native
# Qt editor's --verify-ui-layout contract does not apply to this artifact.

# A production player with no arguments should enter its event loop under both
# Linux display stacks. A bounded timeout is intentional: this is a launch
# smoke test, not an automated gameplay session.
set +e
xvfb-run -a -s '-screen 0 1024x768x24' timeout --signal=TERM 5 \
	"${audit_root}/squashfs-root/AppRun" >"${audit_root}/xvfb.log" 2>&1
xvfb_status=$?
set -e
if [[ ${xvfb_status} -ne 124 && ${xvfb_status} -ne 143 ]]; then
	echo "AppImage did not remain alive under Xvfb (status ${xvfb_status})." >&2
	cat "${audit_root}/xvfb.log" >&2
	exit 1
fi

wayland_socket=fairywriter-wayland
export XDG_RUNTIME_DIR="${work_root}/runtime"
mkdir -p "${XDG_RUNTIME_DIR}"
chmod 700 "${XDG_RUNTIME_DIR}"
weston --backend=headless-backend.so --socket="${wayland_socket}" \
	--idle-time=0 --width=1024 --height=768 >"${audit_root}/weston.log" 2>&1 &
weston_pid=$!
cleanup_weston() { kill "${weston_pid}" 2>/dev/null || true; }
trap 'cleanup; cleanup_weston' EXIT
sleep 1
set +e
WAYLAND_DISPLAY="${wayland_socket}" QT_QPA_PLATFORM=wayland timeout --signal=TERM 5 \
	"${audit_root}/squashfs-root/AppRun" >"${audit_root}/wayland.log" 2>&1
wayland_status=$?
set -e
cleanup_weston
trap cleanup EXIT
if [[ ${wayland_status} -ne 124 && ${wayland_status} -ne 143 ]]; then
	echo "AppImage did not remain alive under headless Weston (status ${wayland_status})." >&2
	cat "${audit_root}/weston.log" "${audit_root}/wayland.log" >&2
	exit 1
fi
if find "${audit_root}/squashfs-root" -type f \
	\( -iname '*.sfc' -o -iname '*.smc' -o -iname 'SCRATCH.MD' \) \
	-print -quit | grep -q .; then
	echo "ROM or scratch data was found in the AppImage." >&2
	exit 1
fi

(
	cd "$(dirname "${artifact}")"
	sha256sum "$(basename "${artifact}")" > "$(basename "${artifact}").sha256"
)
install -m 0644 /src/COPYING /out/COPYING
install -m 0644 /src/TESTING.md /out/TESTING.md
echo "Created ${artifact}"
