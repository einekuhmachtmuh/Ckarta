#!/bin/sh
set -eu

binary=${1:?usage: $0 path/to/ckarta-smoke}

output=$($binary)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F 'CKARTA_JAVA_READY'
printf '%s\n' "$output" | grep -F 'CKARTA_DISPATCH '
printf '%s\n' "$output" | grep -F 'CKARTA_JAVA_STOP'
printf '%s\n' "$output" | grep -F 'CKARTA_SMOKE_OK'
