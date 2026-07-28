#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [[ $# != 2 ]]; then
	echo "usage: extract-appimage APPIMAGE OUTPUT_DIRECTORY" >&2
	exit 2
fi

image=$1
output=$2

# A type-2 AppImage is an ELF launcher followed immediately by SquashFS. Derive
# that boundary from the ELF header itself. Searching for the bytes "hsqs" is
# incorrect because the launcher and compressed payload can contain that byte
# sequence before the real filesystem superblock.
offset=$(readelf -hW "${image}" | awk '
	/Start of section headers:/ { start = $5 }
	/Size of section headers:/ { size = $5 }
	/Number of section headers:/ { count = $5 }
	END {
		if (start > 0 && size > 0 && count > 0)
			print start + (size * count)
	}
')

if [[ -z ${offset} || ${offset} -le 0 ]]; then
	echo "Could not derive the SquashFS offset from ${image}." >&2
	exit 1
fi

mkdir -p "$(dirname "${output}")"
unsquashfs -quiet -no-progress -strict-errors \
	-offset "${offset}" \
	-dest "${output}" \
	"${image}"
