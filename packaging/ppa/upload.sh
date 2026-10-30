#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Signs the source packages from build-source.sh with your OpenPGP key (the one
# registered on Launchpad) and uploads them to the PPA, like debsign and dput.
#   packaging/ppa/upload.sh <key fingerprint>            sign and upload
#   SIGN_ONLY=1 packaging/ppa/upload.sh <key fingerprint> sign only
# PPA=~owner/ubuntu/name chooses another PPA.
set -eu
cd "$(dirname "$0")/../.."
key=${1:?usage: $0 <key fingerprint>}
ppa=${PPA:-~mithicalaviator85/ubuntu/kenning}
shopt -s nullglob
changes_files=(dist/ppa/*_source.changes)
[[ ${#changes_files[@]} -gt 0 ]] || { echo "No source packages in dist/ppa; run packaging/ppa/build-source.sh" >&2; exit 1; }

signed() { grep -q '^-----BEGIN PGP SIGNED MESSAGE-----' "$1"; }
sign() {
    gpg --local-user "$key" --clearsign --output "$1.asc" "$1"
    mv "$1.asc" "$1"
}

# The files a .changes lists, from its Files section.
listed() {
    awk '/^Files:/ { on = 1; next } /^[^ ]/ { on = 0 } on && NF == 5 { print $5 }' "$1"
}

for changes in "${changes_files[@]}"; do
    if signed "$changes"; then
        echo "$changes already signed"
        continue
    fi
    dsc=${changes%_source.changes}.dsc
    signed "$dsc" || sign "$dsc"
    # Signing changed the .dsc, so its sizes and sums in the .changes change too.
    python3 -I - "$changes" "$dsc" << 'PY'
import hashlib, os, sys
changes, dsc = sys.argv[1], sys.argv[2]
name, data = os.path.basename(dsc), open(dsc, "rb").read()
sums = {"Checksums-Sha1:": hashlib.sha1, "Checksums-Sha256:": hashlib.sha256, "Files:": hashlib.md5}
lines, section = [], None
for line in open(changes).read().split("\n"):
    if line and not line.startswith(" "):
        section = line.split()[0] if line.split() else None
    fields = line.split()
    if line.startswith(" ") and fields and fields[-1] == name and section in sums:
        fields[0], fields[1] = sums[section](data).hexdigest(), str(len(data))
        line = " " + " ".join(fields)
    lines.append(line)
open(changes, "w").write("\n".join(lines))
PY
    sign "$changes"
    echo "signed $changes"
done

[[ ${SIGN_ONLY:-} ]] && exit 0
for changes in "${changes_files[@]}"; do
    # The .changes goes last: Launchpad processes an upload once it arrives.
    for file in $(listed "$changes") "${changes##*/}"; do
        echo "uploading $file"
        curl -fsS --ftp-pasv -T "dist/ppa/$file" "ftp://ppa.launchpad.net/$ppa/"
    done
done
echo "Uploaded. Launchpad emails the result; builds appear at https://launchpad.net/${ppa%%/*}/+archive/ubuntu/${ppa##*/}"
