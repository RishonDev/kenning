#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makes the Launchpad PPA source packages for a release, one per Ubuntu series,
# into dist/ppa/ (unsigned; packaging/ppa/upload.sh signs and uploads them).
# The original tarball is the release tag plus the two recognition networks, since
# Launchpad's builders have no network; debian/ comes from packaging/debian.
#   packaging/ppa/build-source.sh [version]     (default: the RPM spec's version)
#   REVISION=2 packaging/ppa/build-source.sh   upload the same release again (for
#                                               example after enabling an architecture)
set -eu
cd "$(dirname "$0")/../.."
version=${1:-$(awk '/^Version:/ { print $2 }' packaging/rpm/kenning.spec)}
revision=${REVISION:-1}
# Ubuntu version and series; a higher version for a newer series keeps upgrades working.
series=("26.04 resolute" "26.10 stonking")
# Any image with dpkg-dev; the cached test image, or plain Ubuntu.
image=${IMAGE:-ubuntu:26.04}
docker image inspect kenning-test/ubuntu_latest > /dev/null 2>&1 && image=${IMAGE:-kenning-test/ubuntu_latest}

out=dist/ppa
rm -rf "$out"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

bash tests/containers/fetch-sources.sh > /dev/null
git archive --prefix="kenning-$version/" "v$version" | tar -x -C "$work"
cp face_detection_yunet_2023mar.onnx face_recognition_sface_2021dec.onnx "$work/kenning-$version/"
tar -C "$work" --sort=name --owner=0 --group=0 --mtime="$(git log -1 --format=%cI "v$version")" \
    -cJf "$out/kenning_$version.orig.tar.xz" "kenning-$version"

maintainer=$(sed -n 's/^ -- \(.*>\)  .*/\1/p' packaging/debian/changelog | head -1)
for entry in "${series[@]}"; do
    ubuntu=${entry% *}
    codename=${entry#* }
    dir=$work/$codename/kenning-$version
    mkdir -p "$work/$codename"
    tar -C "$work/$codename" -xJf "$out/kenning_$version.orig.tar.xz"
    cp -r packaging/debian "$dir/debian"
    {
        echo "kenning ($version-1~ppa$ubuntu.$revision) $codename; urgency=medium"
        echo
        if [[ $revision == 1 ]]; then
            echo "  * Build for Ubuntu $ubuntu."
        else
            echo "  * Build for Ubuntu $ubuntu again, for all architectures."
        fi
        echo
        echo " -- $maintainer  $(date -R)"
        echo
        cat packaging/debian/changelog
    } > "$dir/debian/changelog"
    cp "$out/kenning_$version.orig.tar.xz" "$work/$codename/"
done
chmod -R a+rwX "$work"

# The first revision uploads the original tarball; later ones reuse Launchpad's copy.
sources=-sa
[[ $revision == 1 ]] || sources=-sd
docker run --rm -e sources=$sources -v "$work:/w" -v "$PWD/$out:/out" "$image" bash -c '
    set -e
    for dir in /w/*/kenning-*; do
        cd "$dir"
        dpkg-source -b . > /dev/null
        changes=$(dpkg-parsechangelog -S Source)_$(dpkg-parsechangelog -S Version)_source.changes
        dpkg-genchanges -S $sources > "../$changes" 2> /dev/null
        cd ..
        cp ./*.dsc ./*.debian.tar.xz "$changes" /out/
    done
    chmod a+rw /out/*'
ls -1 "$out"
