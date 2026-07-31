#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_root=${FAIRYWRITER_MAC_BUILD:-"${source_root}/build-arm64"}
release_root=${FAIRYWRITER_MAC_OUTPUT:-"${source_root}/release-macos-arm64"}
qt_root=${QTDIR:-/opt/homebrew/opt/qt}
cmake_bin=${FAIRYWRITER_CMAKE:-/opt/homebrew/bin/cmake}
version=$(tr -d '\r\n' < "${source_root}/VERSION")
if [[ ! ${version} =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
	echo "VERSION must contain one MAJOR.MINOR.PATCH number." >&2
	exit 1
fi
artifact_basename="FairyWriter-${version}-macos-arm64.dmg"
artifact="${release_root}/${artifact_basename}"

if [[ $(uname -m) != arm64 ]]; then
	echo "The tester DMG must be built by a native arm64 process." >&2
	exit 1
fi
for required in "${cmake_bin}" "${qt_root}/bin/macdeployqt" /usr/bin/codesign /usr/bin/hdiutil; do
	if [[ ! -x ${required} ]]; then
		echo "Required macOS packaging tool is missing: ${required}" >&2
		exit 1
	fi
done
qt_plugin_root=$("${qt_root}/bin/qtpaths" --query QT_INSTALL_PLUGINS)
for required_plugin in \
	"${qt_plugin_root}/platforms/libqcocoa.dylib" \
	"${qt_plugin_root}/styles/libqmacstyle.dylib"; do
	if [[ ! -f ${required_plugin} ]]; then
		echo "Required macOS Qt plugin is missing: ${required_plugin}" >&2
		exit 1
	fi
done

# This volume has skipped cartridge regeneration on timestamp comparisons in
# the past. Force the source-owned ROM and its embedded copy through the build
# before trusting the tests or packaging output.
touch "${source_root}/tools/fairywriter-rom/main.go"
"${cmake_bin}" -S "${source_root}" -B "${build_root}" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="${qt_root}" \
	-DFAIRYWRITER_DEVELOPER_TOOLS=OFF \
	-DBUILD_TESTING=ON
"${cmake_bin}" --build "${build_root}" --parallel
ctest --test-dir "${build_root}" --output-on-failure

source_app="${build_root}/FairyWriter.app"
if [[ ! -x "${source_app}/Contents/MacOS/FairyWriter" ]]; then
	echo "Validated FairyWriter.app was not produced at ${source_app}." >&2
	exit 1
fi

work_root=$(mktemp -d /tmp/fairywriter-macos.XXXXXX)
cleanup()
{
	rm -rf "${work_root}"
}
trap cleanup EXIT

volume_root="${work_root}/FairyWriter ${version}"
staged_app="${volume_root}/FairyWriter.app"
mkdir -p "${volume_root}" "${release_root}"
cp -R "${source_app}" "${staged_app}"
cp "${source_root}/COPYING" "${volume_root}/COPYING"
cp "${source_root}/third_party/cmark-gfm/COPYING" \
	"${volume_root}/CMARK-GFM-LICENSE"
cp "${source_root}/TESTING.md" "${volume_root}/TESTING.md"

# macdeployqt otherwise copies every image/text/accessibility plugin installed
# in Homebrew. Several of those optional plugins depend on QtPdf, QtSvg,
# QtVirtualKeyboard, WebP, and Brotli even though FairyWriter never links or
# loads them. Deploy the executable's frameworks, then install only the two
# plugins the actual macOS window needs.
"${qt_root}/bin/macdeployqt" "${staged_app}" \
	-always-overwrite -no-plugins -no-codesign
mkdir -p \
	"${staged_app}/Contents/PlugIns/platforms" \
	"${staged_app}/Contents/PlugIns/styles"
cp "${qt_plugin_root}/platforms/libqcocoa.dylib" \
	"${staged_app}/Contents/PlugIns/platforms/"
cp "${qt_plugin_root}/styles/libqmacstyle.dylib" \
	"${staged_app}/Contents/PlugIns/styles/"
for plugin in \
	"${staged_app}/Contents/PlugIns/platforms/libqcocoa.dylib" \
	"${staged_app}/Contents/PlugIns/styles/libqmacstyle.dylib"; do
	if /usr/bin/otool -l "${plugin}" |
		grep -Fq '@loader_path/../../../../lib'; then
		/usr/bin/install_name_tool -delete_rpath \
			'@loader_path/../../../../lib' "${plugin}"
	fi
	/usr/bin/install_name_tool -add_rpath \
		'@loader_path/../../Frameworks' "${plugin}"
done
"${cmake_bin}" -DFAIRYWRITER_BUNDLE="${staged_app}" \
	-P "${source_root}/cmake/PruneProductionBundle.cmake"
/usr/bin/codesign --force --deep --sign - "${staged_app}"
/usr/bin/codesign --verify --deep --strict "${staged_app}"

architectures=$(/usr/bin/lipo -archs "${staged_app}/Contents/MacOS/FairyWriter")
if [[ ${architectures} != arm64 ]]; then
	echo "Tester app must be arm64-only; found: ${architectures}" >&2
	exit 1
fi
if ! "${staged_app}/Contents/MacOS/FairyWriter" --version |
	grep -Fx "FairyWriter ${version}"; then
	echo "Deployed tester app did not report the expected version." >&2
	exit 1
fi
if find "${staged_app}" -type f -perm -111 -print0 |
	xargs -0 /usr/bin/otool -L 2>/dev/null |
	grep -E '/opt/homebrew|/usr/local'; then
	echo "Deployed tester app still references a developer-machine library." >&2
	exit 1
fi
if find "${staged_app}" -type f \
	\( -iname '*.sfc' -o -iname '*.smc' -o -iname 'SCRATCH.MD' \) \
	-print -quit | grep -q .; then
	echo "Loose ROM or scratch data was found in the tester app." >&2
	exit 1
fi

rm -f "${artifact}" "${artifact}.sha256"
/usr/bin/hdiutil create -quiet -fs HFS+ -format UDZO \
	-volname "FairyWriter ${version}" -srcfolder "${volume_root}" "${artifact}"
/usr/bin/hdiutil verify "${artifact}"
(
	cd "${release_root}"
	shasum -a 256 "${artifact_basename}" > "${artifact_basename}.sha256"
)
echo "Created ${artifact}"
