#!/usr/bin/env bash
# Deterministic replay baseline: two identical runs of a clock-insensitive
# fixture through the libretro core, with the deterministic-timing option on,
# must present byte-identical frames.  The option fixes the virtual clock to
# one 60 Hz step per retro_run and seeds default Random instances from a VM
# counter, so replay/netplay has a reproducible starting point.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HARNESS="${HARNESS:-$ROOT/build/test_libretro_frontend}"
CORE="${CORE:-$ROOT/build/libretro/nano_j2me_libretro.so}"
JAR="${JAR:-$ROOT/tests/fixtures/alphademo.jar}"
FRAMES="${FRAMES:-60}"

for f in "$HARNESS" "$CORE" "$JAR"; do
    if [ ! -f "$f" ]; then
        echo "SKIP: missing $f" >&2
        exit 77
    fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

run() {
    local dir="$1" log="$2"
    mkdir -p "$dir"
    "$HARNESS" --core "$CORE" --jar "$JAR" --frames "$FRAMES" \
        --dump-dir "$dir" --dump-every 1 \
        --set nano_j2me_deterministic=enabled >"$log" 2>&1
}

run "$WORK/a" "$WORK/a.log"
run "$WORK/b" "$WORK/b.log"

if ! grep -q 'key=nano_j2me_deterministic' "$WORK/a.log"; then
    echo "FAIL: the core did not advertise the deterministic-timing option" >&2
    exit 1
fi

count_a=$(find "$WORK/a" -name '*.ppm' | wc -l)
count_b=$(find "$WORK/b" -name '*.ppm' | wc -l)
if [ "$count_a" -ne "$count_b" ]; then
    echo "FAIL: runs presented different frame counts ($count_a vs $count_b)" >&2
    exit 1
fi
if [ "$count_a" -eq 0 ]; then
    echo "FAIL: the fixture presented no frames" >&2
    exit 1
fi

diff <(cd "$WORK/a" && sha256sum ./*.ppm | awk '{print $1}') \
     <(cd "$WORK/b" && sha256sum ./*.ppm | awk '{print $1}') >/dev/null || {
    echo "FAIL: two deterministic-timing runs differ" >&2
    diff <(cd "$WORK/a" && sha256sum ./*.ppm) \
         <(cd "$WORK/b" && sha256sum ./*.ppm) | head -10 >&2 || true
    exit 1
}

echo "PASS: deterministic-timing runs are byte-identical ($count_a frames)"
