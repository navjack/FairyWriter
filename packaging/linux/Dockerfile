# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

# Keep the old Ubuntu 22.04/glibc baseline for broad x86_64 compatibility, but
# use Qt's official relocatable 6.8.3 binaries. This makes Linux and Windows
# target the same Qt feature release instead of hiding two major releases of
# drift behind the distribution package manager.
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		ca-certificates \
		cmake \
		curl \
		file \
		gettext \
		golang-go \
		libfuse2 \
		libdbus-1-3 \
		libegl1-mesa-dev \
		libfontconfig1 \
		libfreetype6 \
		libgl1-mesa-dev \
		libglib2.0-0 \
		libice6 \
		libsm6 \
		libwayland-client0 \
		libwayland-egl1 \
		libx11-6 \
		libx11-xcb1 \
		libxau6 \
		libxcb1 \
		libxcb-cursor0 \
		libxcb-icccm4 \
		libxcb-image0 \
		libxcb-keysyms1 \
		libxcb-randr0 \
		libxcb-render-util0 \
		libxcb-shape0 \
		libxcb-shm0 \
		libxcb-sync1 \
		libxcb-xfixes0 \
		libxcb-xinerama0 \
		libxdmcp6 \
		libxext6 \
		libxkbcommon-x11-0 \
		libxkbcommon-dev \
		libxrender1 \
		ninja-build \
		patchelf \
		p7zip-full \
		python3 \
		squashfs-tools \
		weston \
		xvfb \
		zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*

ARG QT_VERSION=6.8.3
ARG QT_BUILD=6.8.3-0-202503201345
ARG QT_REPOSITORY=https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_683/qt6_683/qt.qt6.683.linux_gcc_64
RUN set -eu; \
	qt_root="/opt/qt/${QT_VERSION}/gcc_64"; \
	mkdir -p /tmp/qt-packages "${qt_root}"; \
	for package in qtbase qtwayland; do \
		archive="${QT_BUILD}${package}-Linux-RHEL_8_10-GCC-Linux-RHEL_8_10-X86_64.7z"; \
		curl -fL --retry 3 -o "/tmp/qt-packages/${archive}" "${QT_REPOSITORY}/${archive}"; \
		curl -fL --retry 3 -o "/tmp/qt-packages/${archive}.sha1" "${QT_REPOSITORY}/${archive}.sha1"; \
		printf '%s  %s\n' "$(cat "/tmp/qt-packages/${archive}.sha1")" "/tmp/qt-packages/${archive}" | sha1sum --check --strict; \
		7z x -y "/tmp/qt-packages/${archive}" "-o${qt_root}" >/dev/null; \
	done; \
	archive="${QT_BUILD}icu-linux-Rhel8.6-x86_64.7z"; \
	curl -fL --retry 3 -o "/tmp/qt-packages/${archive}" "${QT_REPOSITORY}/${archive}"; \
	curl -fL --retry 3 -o "/tmp/qt-packages/${archive}.sha1" "${QT_REPOSITORY}/${archive}.sha1"; \
	printf '%s  %s\n' "$(cat "/tmp/qt-packages/${archive}.sha1")" "/tmp/qt-packages/${archive}" | sha1sum --check --strict; \
	7z x -y "/tmp/qt-packages/${archive}" "-o${qt_root}/lib" >/dev/null; \
	printf '[Paths]\nPrefix=..\n' > "${qt_root}/bin/qt.conf"; \
	"${qt_root}/bin/qmake" -query QT_VERSION | grep -Fx "${QT_VERSION}"; \
	rm -rf /tmp/qt-packages

COPY packaging/linux/extract-appimage.sh /usr/local/bin/extract-appimage
RUN chmod +x /usr/local/bin/extract-appimage

ARG LINUXDEPLOY_VERSION=1-alpha-20250213-2
ARG LINUXDEPLOY_QT_VERSION=1-alpha-20250213-1
ARG LINUXDEPLOY_APPIMAGE_VERSION=1-alpha-20250213-1
RUN set -eu; \
	mkdir -p /opt/linuxdeploy \
	&& curl -fL --retry 3 -o /opt/linuxdeploy/linuxdeploy \
		"https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage" \
	&& curl -fL --retry 3 -o /opt/linuxdeploy/linuxdeploy-plugin-qt \
		"https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_QT_VERSION}/linuxdeploy-plugin-qt-x86_64.AppImage" \
	&& curl -fL --retry 3 -o /opt/linuxdeploy/linuxdeploy-plugin-appimage \
		"https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/${LINUXDEPLOY_APPIMAGE_VERSION}/linuxdeploy-plugin-appimage-x86_64.AppImage" \
	&& printf '%s  %s\n' \
		'4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a' /opt/linuxdeploy/linuxdeploy \
		'15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724' /opt/linuxdeploy/linuxdeploy-plugin-qt \
		'992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3' /opt/linuxdeploy/linuxdeploy-plugin-appimage \
		| sha256sum --check --strict \
	&& for tool in linuxdeploy linuxdeploy-plugin-qt linuxdeploy-plugin-appimage; do \
		mv "/opt/linuxdeploy/${tool}" "/opt/linuxdeploy/${tool}.AppImage"; \
		extract-appimage "/opt/linuxdeploy/${tool}.AppImage" "/opt/linuxdeploy/${tool}.AppDir"; \
		ln -s "/opt/linuxdeploy/${tool}.AppDir/AppRun" "/opt/linuxdeploy/${tool}"; \
	done

ENV QTDIR="/opt/qt/6.8.3/gcc_64"
ENV PATH="${QTDIR}/bin:/opt/linuxdeploy:${PATH}"
ENV CMAKE_PREFIX_PATH="${QTDIR}"
ENV LD_LIBRARY_PATH="${QTDIR}/lib"
ENV QT_PLUGIN_PATH="${QTDIR}/plugins"
ENV QT_QPA_PLATFORM=offscreen
WORKDIR /src
