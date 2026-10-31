#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makes the AUR packaging from the repository's PKGBUILD. An AUR clone has no Kenning
# source, so these download it instead of building their own checkout:
#   packaging/aur/kenning/      the release the PKGBUILD names, from its GitHub tag
#   packaging/aur/kenning-git/  the latest master (kenning-git, plasma-login-manager-kenning-git)
# Each is one AUR repository: copy its PKGBUILD, kenning.install and .SRCINFO there.
#   packaging/aur/update.sh        (after tagging and pushing a release)
set -eu
cd "$(dirname "$0")/../.."
version=$(sed -n 's/^pkgver=//p' PKGBUILD)
sum=$(curl -fsSL "https://github.com/RishonDev/kenning/archive/refs/tags/v$version.tar.gz" | sha256sum | cut -d' ' -f1)
mkdir -p packaging/aur/kenning packaging/aur/kenning-git

python3 -I - "$sum" << 'PY'
import sys
release_sum = sys.argv[1]
original = open("PKGBUILD").read()

def make(git):
    header, _, body = original.partition("\npkgbase=")
    s = ("# Maintainer: Rishon Jonathan R <kenning@invalid>\n"
         "# Generated from the Kenning repository's PKGBUILD by packaging/aur/update.sh\n"
         + header + "\npkgbase=" + body)
    def once(old, new):
        nonlocal s
        assert s.count(old) == 1, old
        s = s.replace(old, new)
    source = "$srcdir/kenning" if git else "$srcdir/kenning-$pkgver"
    if git:
        once("source=(", 'source=("kenning::git+https://github.com/RishonDev/kenning.git"\n        ')
        once("sha256sums=(", "sha256sums=('SKIP'\n            ")
        once("makedepends=(", "makedepends=(git ")
        once("pkgbase=kenning\n", "pkgbase=kenning-git\n")
        once("pkgname=(kenning plasma-login-manager-kenning)", "pkgname=(kenning-git plasma-login-manager-kenning-git)")
        once("\nprepare() {", """
pkgver() {
  cd kenning
  git describe --long --tags | sed 's/^v//; s/\\([^-]*-g\\)/r\\1/; s/-/./g'
}

prepare() {""")
        once("package_kenning() {", "package_kenning-git() {\n  provides=(\"kenning=${pkgver%%.r*}\")")
        once("  conflicts=(plasma-faceunlock)", "  conflicts=(kenning plasma-faceunlock)")
        once("package_plasma-login-manager-kenning() {", "package_plasma-login-manager-kenning-git() {")
        once('  provides=("plasma-login-manager=$_plmver")', '  provides=("plasma-login-manager=$_plmver" "plasma-login-manager-kenning=${pkgver%%.r*}")')
        once("  conflicts=(plasma-login-manager plasma-login-manager-faceunlock)",
             "  conflicts=(plasma-login-manager plasma-login-manager-kenning plasma-login-manager-faceunlock)")
    else:
        once("source=(", 'source=("kenning-$pkgver.tar.gz::https://github.com/RishonDev/kenning/archive/refs/tags/v$pkgver.tar.gz"\n        ')
        once("sha256sums=(", f"sha256sums=('{release_sum}'\n            ")
    once("install=packaging/arch/kenning.install", "install=kenning.install")
    s = s.replace('"$startdir"', f'"{source}"').replace("$startdir/", f"{source}/")
    assert "$startdir" not in s, "a $startdir use the AUR build can't follow"
    return s

open("packaging/aur/kenning/PKGBUILD", "w").write(make(False))
open("packaging/aur/kenning-git/PKGBUILD", "w").write(make(True))
PY
for dir in packaging/aur/kenning packaging/aur/kenning-git; do
    cp packaging/arch/kenning.install "$dir/"
    (cd "$dir" && makepkg --printsrcinfo > .SRCINFO)
done
echo "packaging/aur: kenning $version ($sum) and kenning-git"
