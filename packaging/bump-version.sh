#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: CC0-1.0
#
# Sets the version everywhere it is written down and adds a changelog entry to the
# Debian and RPM packaging:
#   packaging/bump-version.sh 0.7.0 "Photos of failed attempts" "Try Face Again on the login screen"
# Then review with git diff, commit, and tag v<version>.
set -eu
cd "$(dirname "$0")/.."
version=${1:?usage: $0 <version> <changelog line>...}
shift
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Version must look like 1.2.3" >&2; exit 1; }
[[ $# -gt 0 ]] || { echo "Give at least one changelog line" >&2; exit 1; }

maintainer=$(sed -n 's/^ -- \(.*>\)  .*/\1/p' packaging/debian/changelog | head -1)

sed -i "s/^project(kenning VERSION [0-9.]*/project(kenning VERSION $version/" CMakeLists.txt
sed -i "s/^pkgver=.*/pkgver=$version/; s/^pkgrel=.*/pkgrel=1/" PKGBUILD
sed -i "s/^Version:\( *\).*/Version:\1$version/; s/^Release:\( *\).*/Release:\11%{?dist}/" packaging/rpm/kenning.spec

{
    echo "kenning ($version-1) unstable; urgency=medium"
    echo
    for line in "$@"; do
        echo "  * $line" | fold -s -w 78 | sed 's/ *$//; 2,$s/^/    /'
    done
    echo
    echo " -- $maintainer  $(date -R)"
    echo
    cat packaging/debian/changelog
} > packaging/debian/changelog.new
mv packaging/debian/changelog.new packaging/debian/changelog

entry="* $(LC_ALL=C date '+%a %b %d %Y') $maintainer - $version-1"
for line in "$@"; do
    entry+=$'\n'"- $line"
done
python3 -I - "$entry" << 'PY'
import sys
path = "packaging/rpm/kenning.spec"
spec = open(path).read()
head, marker, rest = spec.partition("%changelog\n")
assert marker, "no %changelog in the spec"
open(path, "w").write(head + marker + sys.argv[1] + "\n\n" + rest)
PY

echo "Version $version written to CMakeLists.txt, PKGBUILD, the RPM spec and the Debian changelog."
