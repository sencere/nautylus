#!/bin/sh
set -eu

cli=${1:-./build/nautylus}
out_dir=${2:-build/profile}
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"
mkdir -p "$out_dir"

for n in 128 1000 5000; do
    db="$out_dir/bench-$n.ng"
    rm -f "$db"
    "$cli" bench "$db" "$n" > "$out_dir/bench-$n.txt"
    cat "$out_dir/bench-$n.txt"
done
