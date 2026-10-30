#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds and tests Kenning on several distributions in containers.
#   tests/containers/run.sh [image ...]
# Run from the repository root after makepkg has downloaded the two .onnx networks.
set -u
cd "$(dirname "$0")/../.."
images=("$@")
[[ ${#images[@]} -gt 0 ]] || images=(archlinux:latest debian:trixie ubuntu:25.04 fedora:42)
mkdir -p tests/containers/logs
for image in "${images[@]}"; do
    log="tests/containers/logs/${image//[\/:]/_}.log"
    echo "== $image (log: $log)"
    docker run --rm -v "$PWD:/src:ro" "$image" bash /src/tests/containers/in-container.sh > "$log" 2>&1
    grep "^RESULT\|^   " "$log"
done
