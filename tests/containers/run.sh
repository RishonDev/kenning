#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds and tests Kenning on several distributions in containers.
#   tests/containers/run.sh [image ...]
# Run from the repository root after makepkg has downloaded the two .onnx networks.
# Defaults to the newest release of each; openSUSE (opensuse/tumbleweed:latest) is
# left out because its mirrors are slow, but can be named.
set -u
cd "$(dirname "$0")/../.." || exit 1
images=("$@")
[[ ${#images[@]} -gt 0 ]] || images=(archlinux:latest debian:latest ubuntu:latest ubuntu:rolling fedora:latest)
mkdir -p tests/containers/logs
# The first run of an image keeps its container, packages installed, as
# kenning-test/<image>; later runs start from that and download nothing.
# Remove them with: docker rmi $(docker images -q 'kenning-test/*')
for image in "${images[@]}"; do
    name=${image//[\/:]/_}
    log="tests/containers/logs/$name.log"
    cached="kenning-test/$name"
    echo "== $image (log: $log)"
    if docker image inspect "$cached" > /dev/null 2>&1; then
        docker run --rm -e KENNING_DEPS_INSTALLED=1 -v "$PWD:/src:ro" "$cached" bash /src/tests/containers/in-container.sh > "$log" 2>&1
    else
        docker rm -f "kenning-test-$name" > /dev/null 2>&1
        docker run --name "kenning-test-$name" -v "$PWD:/src:ro" "$image" bash /src/tests/containers/in-container.sh > "$log" 2>&1
        grep -q "^RESULT dependencies: PASS" "$log" && docker commit "kenning-test-$name" "$cached" > /dev/null
        docker rm "kenning-test-$name" > /dev/null
    fi
    grep "^RESULT\|^   " "$log"
done
