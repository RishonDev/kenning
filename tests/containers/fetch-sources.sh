#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Downloads the sources the PKGBUILD lists (the recognition networks and the Plasma
# Login Manager release) into the repository root, where every package build looks
# for them, and checks them against the PKGBUILD's sums. Files already there and
# matching are kept.
#   tests/containers/fetch-sources.sh
set -eu
cd "$(dirname "$0")/../.."
mapfile -t entries < <(bash -c '. ./PKGBUILD; for i in "${!source[@]}"; do echo "${source[$i]} ${sha256sums[$i]}"; done')
for entry in "${entries[@]}"; do
    url=${entry% *}
    sum=${entry##* }
    file=${url##*/}
    if [[ ! -f $file ]] || [[ $sum != SKIP && $(sha256sum "$file" | cut -d' ' -f1) != "$sum" ]]; then
        echo "fetching $file"
        curl -fsSL --retry 3 -o "$file" "$url"
    fi
    if [[ $sum != SKIP ]] && ! echo "$sum  $file" | sha256sum -c --quiet -; then
        echo "$file doesn't match the PKGBUILD's sum" >&2
        exit 1
    fi
done
