#!/usr/bin/env bash
# Dependencies: build-essential pkg-config python3 libglib2.0-dev libpixman-1-dev zlib1g-dev curl xz-utils.
set -eu
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/oslab"
mkdir -p "$cache_dir"
cd "$cache_dir"
archive=qemu-6.2.0.tar.xz
if [ ! -f "$archive" ]; then
    curl --fail --location --retry 2 https://download.qemu.org/qemu-6.2.0.tar.xz -o "$archive"
fi
printf '%s  %s\n' 68e15d8e45ac56326e0b9a4afa8b49a3dfe8aba3488221d098c84698bca65b45 "$archive" | sha256sum -c -
if [ ! -d qemu-6.2.0 ]; then
    tar -xJf "$archive"
fi
mkdir -p qemu-6.2.0/build-debug
cd qemu-6.2.0/build-debug
../configure --target-list=riscv64-softmmu --enable-debug --disable-werror \
    --disable-docs --disable-gtk --disable-sdl --disable-vnc > configure.log 2>&1
make -j"${OSLAB_BUILD_JOBS:-3}" > build.log 2>&1
./qemu-system-riscv64 --version
printf 'Debug executable: %s/qemu-system-riscv64\n' "$PWD"
# Intentionally no make install: the system QEMU remains available for regression.
