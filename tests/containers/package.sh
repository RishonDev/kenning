#!/bin/bash
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the distribution package for each image and tests installing, using and
# removing it. Uses the kenning-test/<image> images from run.sh (run that first).
#   tests/containers/package.sh [image ...]      packages land in dist/<image>/
set -u
cd "$(dirname "$0")/../.." || exit 1
images=("$@")
[[ ${#images[@]} -gt 0 ]] || images=(ubuntu:latest ubuntu:rolling fedora:latest)
mkdir -p tests/containers/logs
for image in "${images[@]}"; do
    name=${image//[\/:]/_}
    cached="kenning-test/$name"
    if ! docker image inspect "$cached" > /dev/null 2>&1; then
        echo "== $image: no $cached image; run tests/containers/run.sh $image first"
        continue
    fi
    mkdir -p "dist/$name"
    chmod 777 "dist/$name"
    log="tests/containers/logs/package-$name.log"
    echo "== $image (log: $log)"
    {
        docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist/$name:/out" "$cached" bash /src/tests/containers/in-container-package.sh build &&
            docker run --rm -v "$PWD:/src:ro" -v "$PWD/dist/$name:/out:ro" "$cached" bash /src/tests/containers/in-container-package.sh test
    } > "$log" 2>&1
    grep "^RESULT\|^   " "$log"
done
