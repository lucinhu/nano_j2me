#!/bin/sh
# Drive a 3D title through the libretro core with the corpus key script and
# require an animated, non-black picture.  The game JAR lives in the
# gitignored tests/games corpus, so the check skips (exit 77) when it is not
# present; CTest registers it with SKIP_RETURN_CODE 77.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HARNESS=${HARNESS:-$ROOT/build/test_libretro_frontend}
CORE=${CORE:-$ROOT/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so}
JAR=${JAR:-$ROOT/tests/games/浴火银河2完整版_7689.jar}
KEYS=${KEYS:-$ROOT/tools/corpus_keys/浴火银河2完整版_7689.txt}
FRAMES=${FRAMES:-900}
TIMEOUT_S=${TIMEOUT_S:-150}

if [ ! -f "$JAR" ]; then
    echo "check_libretro_3d_keys: skip (corpus JAR not present: $JAR)"
    exit 77
fi
if [ ! -f "$KEYS" ]; then
    echo "check_libretro_3d_keys: skip (key script not present: $KEYS)"
    exit 77
fi
if [ ! -x "$HARNESS" ]; then
    echo "check_libretro_3d_keys: skip (harness not built: $HARNESS)"
    exit 77
fi
if [ ! -f "$CORE" ]; then
    echo "check_libretro_3d_keys: skip (core not built: $CORE)"
    exit 77
fi

exec "$HARNESS" \
    --core "$CORE" \
    --jar "$JAR" \
    --frames "$FRAMES" \
    --pace-us 16667 \
    --keys "$KEYS" \
    --visual-required \
    --min-nonblack 8 \
    --timeout "$TIMEOUT_S"
