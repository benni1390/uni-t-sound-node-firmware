#!/bin/sh
set -eu

libdeps_dir="/workspace/.pio/libdeps/esp32c6"
mkdir -p "$libdeps_dir"

for dependency in /opt/platformio-libdeps/esp32c6/*; do
    [ -e "$dependency" ] || continue
    target="$libdeps_dir/$(basename "$dependency")"
    if [ ! -e "$target" ]; then
        cp -R "$dependency" "$libdeps_dir/"
    fi
done

exec pio "$@"
