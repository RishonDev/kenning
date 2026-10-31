#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Patches a Plasma Login Manager source tree for Kenning, whatever its version:
# applies the newest patch in patches/ that fits without fuzz, then adds the scan
# animation shared with the settings page and the lock screen.
#   login-manager/apply.sh <source dir>
set -eu
here=$(dirname "$(readlink -f "$0")")
tree=${1:?usage: $0 <plasma-login-manager source dir>}

for patch in $(printf '%s\n' "$here"/patches/*.patch | sort -rV); do
    if patch -d "$tree" -p1 --dry-run --fuzz=0 -s -f < "$patch" > /dev/null 2>&1; then
        patch -d "$tree" -p1 --fuzz=0 -s < "$patch"
        install -m644 "$here/../kcm/src/ui/FaceScanIndicator.qml" "$tree/src/frontend/greeter/qml/FaceScanIndicator.qml"
        echo "kenning: applied login-manager/patches/$(basename "$patch")"
        exit 0
    fi
done
echo "kenning: no patch in login-manager/patches fits this Plasma Login Manager" >&2
exit 1
