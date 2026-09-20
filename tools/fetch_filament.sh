#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT HUP INT TERM
curl -fL https://github.com/google/filament/releases/download/v1.77.1/filament-v1.77.1-linux.tgz -o "$archive"
printf '%s  %s\n' ec0f5287a3a2fb801a93fd7b0ffd80c894aac980716d6f91ec48e5370d2d0674 "$archive" | sha256sum -c -
mkdir -p "$root/.deps"
tar -xzf "$archive" -C "$root/.deps"
