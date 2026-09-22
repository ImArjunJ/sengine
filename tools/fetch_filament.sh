#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case $(uname -s) in
    Linux) platform=linux; checksum=ec0f5287a3a2fb801a93fd7b0ffd80c894aac980716d6f91ec48e5370d2d0674 ;;
    Darwin) platform=mac; checksum=ab99d67b456d3917de3e8778330e36a8bdf0f510981484b2c34e72b7f4e20d04 ;;
    *) echo 'Set filament_root to a matching Filament 1.77.1 SDK.' >&2; exit 1 ;;
esac
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT HUP INT TERM
curl -fL "https://github.com/google/filament/releases/download/v1.77.1/filament-v1.77.1-$platform.tgz" -o "$archive"
if command -v sha256sum >/dev/null 2>&1; then
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum -c -
else
    printf '%s  %s\n' "$checksum" "$archive" | shasum -a 256 -c -
fi
mkdir -p "$root/.deps"
tar -xzf "$archive" -C "$root/.deps"
