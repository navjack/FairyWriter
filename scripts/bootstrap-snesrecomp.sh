#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dependency_root=${FAIRYWRITER_SNESRECOMP_ROOT:-"${source_root}/third_party/snesrecomp"}
patch_path="${source_root}/patches/snesrecomp-fairywriter.patch"
repository=https://github.com/navjack/snesrecomp.git
pinned_commit=05a7d44a10be994d23fadcf551188f04ec95cbc3

if [[ -e ${dependency_root} && ! -d ${dependency_root}/.git ]]; then
	echo "Refusing to replace non-Git path: ${dependency_root}" >&2
	exit 1
fi

if [[ ! -d ${dependency_root}/.git ]]; then
	mkdir -p "$(dirname "${dependency_root}")"
	git clone --no-checkout "${repository}" "${dependency_root}"
	git -C "${dependency_root}" fetch --tags origin
	git -C "${dependency_root}" checkout --detach "${pinned_commit}"
fi

origin=$(git -C "${dependency_root}" remote get-url origin)
if [[ ${origin} != "${repository}" && ${origin} != git@github.com:navjack/snesrecomp.git ]]; then
	echo "Unexpected snesrecomp origin: ${origin}" >&2
	exit 1
fi

if git -C "${dependency_root}" apply --reverse --check \
	--ignore-space-change --ignore-whitespace "${patch_path}" >/dev/null 2>&1; then
	if [[ $(git -C "${dependency_root}" rev-parse HEAD) != "${pinned_commit}" ]]; then
		echo "FairyWriter patch is present on an unexpected snesrecomp revision." >&2
		exit 1
	fi
	echo "snesrecomp is already pinned and patched for FairyWriter."
	exit 0
fi

if ! git -C "${dependency_root}" diff --quiet ||
	! git -C "${dependency_root}" diff --cached --quiet ||
	[[ -n $(git -C "${dependency_root}" ls-files --others --exclude-standard) ]]; then
	echo "Refusing to overwrite local changes in ${dependency_root}" >&2
	exit 1
fi

if ! git -C "${dependency_root}" cat-file -e "${pinned_commit}^{commit}" 2>/dev/null; then
	git -C "${dependency_root}" fetch --tags origin
fi
git -C "${dependency_root}" checkout --detach "${pinned_commit}"
git -C "${dependency_root}" apply --check \
	--ignore-space-change --ignore-whitespace "${patch_path}"
git -C "${dependency_root}" apply \
	--ignore-space-change --ignore-whitespace "${patch_path}"

echo "Pinned and patched snesrecomp at ${dependency_root}"
