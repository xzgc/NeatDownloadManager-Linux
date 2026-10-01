#!/bin/sh
# Build helper for this machine: Qt6 dev files are unpacked at ../qt6dev/prefix
# (no system qt6-base-dev installed). Adjust QT_PREFIX if you have a normal setup.
QT_PREFIX="$(dirname "$0")/../qt6dev/prefix/usr"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" "$@" || exit 1
exec cmake --build build
