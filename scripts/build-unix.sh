#!/usr/bin/env sh
set -eu
workspace=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$workspace/build"}
case "$build_dir" in /*) ;; *) build_dir="$workspace/$build_dir" ;; esac
# Pass a universal macOS architecture list explicitly when the Qt kit contains both:
# ./scripts/build-unix.sh '-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64'
if [ "$(uname -s)" = Darwin ]; then
    set -- "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-12.0}" "$@"
fi
cmake -S "$workspace" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
