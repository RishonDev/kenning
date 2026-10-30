#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Inside a kenning-test/<image> container (see package.sh):
#   build   build the .deb or .rpm from /src into /out
#   test    install it from /out, check it, and remove it
set -u
result() { echo "RESULT $1: $2${3:+ ($3)}"; }
. /etc/os-release
case "$ID" in
debian | ubuntu) format=deb ;;
fedora) format=rpm ;;
*) result format FAIL "no package format for $ID"; exit 1 ;;
esac

# The source tree as a package build sees it: the repository without build
# products, plus the two recognition networks.
source_tree() {
    mkdir -p "$1"
    tar -C /src --exclude=./.git --exclude=./src --exclude=./pkg --exclude=./dist --exclude=./tests/containers/logs \
        --exclude='./*.pkg.tar.*' --exclude='./*.tar.xz*' --exclude='./*.bz2' --exclude='./*.dat' -cf - . | tar -C "$1" -xf -
}

build() {
    if [[ $format == deb ]]; then
        export DEBIAN_FRONTEND=noninteractive
        apt-get install -y -qq --no-install-recommends debhelper dpkg-dev fakeroot > /tmp/tools.log 2>&1 ||
            { result "packaging tools" FAIL; tail -5 /tmp/tools.log; return 1; }
        source_tree /build/kenning
        cp -r /build/kenning/packaging/debian /build/kenning/debian
        (cd /build/kenning && dpkg-buildpackage -b -uc -us -jauto) > /tmp/build.log 2>&1 ||
            { result "build package" FAIL; grep -m8 -E "error|Error|unmet|dh_" /tmp/build.log; return 1; }
        cp /build/*.deb /out/
    else
        dnf install -y -q --setopt=install_weak_deps=False rpm-build systemd-rpm-macros > /tmp/tools.log 2>&1 ||
            { result "packaging tools" FAIL; tail -5 /tmp/tools.log; return 1; }
        mkdir -p ~/rpmbuild/{SOURCES,SPECS}
        source_tree /build/kenning-0.4.0
        tar -C /build -czf ~/rpmbuild/SOURCES/kenning-0.4.0.tar.gz kenning-0.4.0
        cp /src/*.onnx ~/rpmbuild/SOURCES/
        rpmbuild -bb /src/packaging/rpm/kenning.spec > /tmp/build.log 2>&1 ||
            { result "build package" FAIL; grep -m8 -E "error|Error|Failed" /tmp/build.log; return 1; }
        cp ~/rpmbuild/RPMS/*/*.rpm /out/
    fi
    result "build package" PASS "$(cd /out && ls | tr '\n' ' ')"
}

check() { # name command...
    local name=$1
    shift
    if out=$("$@" 2>&1); then result "$name" PASS; else result "$name" FAIL "$(tail -2 <<< "$out" | tr '\n' ' ')"; fi
}

test_package() {
    if [[ $format == deb ]]; then
        export DEBIAN_FRONTEND=noninteractive
        apt-get install -y -qq --no-install-recommends /out/kenning_*.deb > /tmp/install.log 2>&1
        installed=$?
        files() { dpkg -L kenning; }
        remove() { apt-get remove -y -qq kenning; }
        lockscreen_package=plasma-desktop-data
        install_lockscreen() { apt-get install -y -qq --no-install-recommends $lockscreen_package; }
    else
        dnf install -y -q --setopt=install_weak_deps=False /out/kenning-0*.rpm > /tmp/install.log 2>&1
        installed=$?
        files() { rpm -ql kenning; }
        remove() { dnf remove -y -q kenning; }
        lockscreen_package=plasma-desktop
        install_lockscreen() { rpm -i --nodeps --excludedocs $(dnf -q download --destdir /tmp/pd plasma-desktop > /dev/null && ls /tmp/pd/plasma-desktop-*.x86_64.rpm); }
    fi
    [[ $installed -eq 0 ]] && result install PASS || { result install FAIL; tail -8 /tmp/install.log; return 1; }

    list=$(files)
    for path in /usr/bin/kenning pam_kenning.so kcm_kenning.so kcmkenning_authhelper /usr/lib/systemd/system/kenning.socket \
        /usr/lib/systemd/system/sockets.target.wants/kenning.socket /usr/lib/sysusers.d/kenning.conf \
        /usr/share/kenning/models/face_detection_yunet_2023mar.onnx /usr/lib/kenning/kenning-lockscreen.py \
        /etc/xdg/autostart/org.kde.kenning.setupprompt.desktop /usr/share/kenning/themes/hello/metadata.json; do
        grep -q "$path" <<< "$list" || missing+=" $path"
    done
    [[ -z ${missing:-} ]] && result "files installed" PASS || result "files installed" FAIL "missing:$missing"
    echo "   PAM module: $(grep pam_kenning.so <<< "$list")"

    check "service user created" id kenning
    check "models folder private" sh -c '[ "$(stat -c %U:%a /etc/kenning/models)" = kenning:700 ]'

    # The service as systemd would run it is out of reach here (no systemd in the
    # container); run it by hand and go through the PAM module by name, so the
    # distribution's own module directory is what finds it.
    /usr/bin/kenning daemon & daemon=$!
    sleep 1
    cat > /tmp/h.c <<'C'
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
static int conv(int n, const struct pam_message **m, struct pam_response **r, void *d) {
    *r = calloc(n, sizeof(struct pam_response));
    for (int i = 0; i < n; i++) printf("message: %s\n", m[i]->msg);
    return PAM_SUCCESS;
}
int main(int argc, char **argv) {
    struct pam_conv c = {conv, NULL}; pam_handle_t *h;
    if (pam_start_confdir("kenning-test", argv[1], &c, "/tmp/pamconf", &h) != PAM_SUCCESS) return 2;
    int r = pam_authenticate(h, 0); printf("result: %s\n", pam_strerror(h, r)); pam_end(h, r); return 0;
}
C
    mkdir -p /tmp/pamconf
    printf 'auth sufficient pam_kenning.so\nauth required pam_deny.so\n' > /tmp/pamconf/kenning-test
    sed -i 's/^disabled *=.*/disabled = false/' /etc/kenning/config.ini
    # A dummy face model makes the module announce a scan, which proves PAM found it
    # by name and it reached the service (the scan itself fails: no camera).
    python3 -c "import json; print(json.dumps([{'id': 0, 'label': 'dummy', 'time': 0, 'data': [[0.0] * 128]}]))" > /etc/kenning/models/root.dat
    gcc -o /tmp/h /tmp/h.c -lpam 2> /dev/null && out=$(/tmp/h root)
    if grep -q "Attempting facial authentication" <<< "${out:-}"; then
        result "PAM module loads by name" PASS "$(grep message <<< "$out" | tail -1)"
    else
        result "PAM module loads by name" FAIL "${out:-harness failed}"
    fi
    rm -f /etc/kenning/models/root.dat
    kill $daemon 2> /dev/null

    # Installing the lock screen fires the package's trigger.
    if install_lockscreen > /tmp/ls.log 2>&1; then
        plasma=$(grep -o "6\.[0-9]*\.[0-9]*" <<< "$(dpkg -s $lockscreen_package 2> /dev/null || rpm -q $lockscreen_package)" | head -1)
        trigger=$(grep -m1 "kenning lock screen" /tmp/ls.log)
        if [[ -z $trigger ]]; then
            result "lock screen trigger (Plasma $plasma)" FAIL "the package's trigger didn't run"
        elif grep -q "kenning lock screen patch" /usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/LockScreenUi.qml; then
            result "lock screen trigger (Plasma $plasma)" PASS "patched"
        else
            result "lock screen trigger (Plasma $plasma)" PASS "${trigger#kenning lock screen: }"
        fi
    else
        result "lock screen add-on" SKIP "couldn't install $lockscreen_package"
    fi

    echo "auth sufficient pam_kenning.so" >> /etc/pam.d/sudo 2> /dev/null || true
    remove > /tmp/remove.log 2>&1 && result remove PASS || result remove FAIL "$(tail -2 /tmp/remove.log)"
    grep -lq pam_kenning.so /etc/pam.d/* 2> /dev/null && result "PAM cleaned on removal" FAIL || result "PAM cleaned on removal" PASS
}

case "${1:-}" in
build) build ;;
test) test_package ;;
*) echo "usage: $0 build|test"; exit 1 ;;
esac
