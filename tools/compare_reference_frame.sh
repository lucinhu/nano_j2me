#!/bin/sh
# Compare one frame of ours with the reference emulator's screenshot.
#
#   tools/compare_reference_frame.sh <nj2me> <jar> <run-freej2me-headless.sh>
#                                    <frame> [width height]
#
# The reference needs a key press after the first wait: a title sitting on a
# splash screen never paints, and the screenshot then has nothing to capture.
# Both runtimes advance animations on their own clocks, so the number this
# prints bounds "same scene, small differences" - it is not a frame-locked
# pixel certificate.  Exits 77 (skip) when PIL or the reference is missing.
set -u

bin=${1:-}; jar=${2:-}; ref=${3:-}; frame=${4:-300}; w=${5:-240}; h=${6:-320}
if [ -z "$bin" ] || [ -z "$jar" ] || [ -z "$ref" ] || [ ! -x "$bin" ] || [ ! -f "$jar" ]; then
    echo "usage: $0 <nj2me> <jar> <run-freej2me-headless.sh> <frame> [w h]" >&2
    exit 2
fi
if [ ! -x "$ref" ]; then
    echo "SKIP: reference runner not present ($ref)"; exit 77
fi
if ! python3 -c 'import PIL' 2>/dev/null; then
    echo "SKIP: python3 has no PIL"; exit 77
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/njrefcmp.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/ours" "$tmp/refdir"

"$bin" --port headless --size "${w}x${h}" --heap 32 --fps 0 \
       --run-frames "$frame" --dump-frames "$tmp/ours/" --dump-every "$frame" \
       "$jar" >"$tmp/ours.log" 2>&1 || { echo "FAIL: our run exited $?"; exit 1; }

printf 'wait 3000\npress 5 200\nwait 7000\nshot %s/ref.png\nquit\n' "$tmp" |
    "$ref" "$jar" "$tmp/refdir" "$w" "$h" >"$tmp/ref.log" 2>&1 ||
    { echo "FAIL: reference run exited $?"; exit 1; }

python3 - "$tmp" <<'PY'
import glob, sys
d = sys.argv[1]
ppms = sorted(glob.glob(d + '/ours/*.ppm'))
pngs = sorted(glob.glob(d + '/**/*.png', recursive=True))
if not ppms:
    print('FAIL: our run produced no frame'); sys.exit(1)
if not pngs:
    print('FAIL: the reference produced no screenshot'); sys.exit(1)
from PIL import Image
a = Image.open(ppms[0]).convert('RGB')
b = Image.open(pngs[0]).convert('RGB')
if a.size != b.size:
    print('FAIL: sizes differ', a.size, b.size); sys.exit(1)
pa, pb = a.load(), b.load()
w, h = a.size
tot = n = 0
for y in range(h):
    for x in range(w):
        for c in range(3):
            tot += abs(pa[x, y][c] - pb[x, y][c]); n += 1
print('MAE %.2f / 255 over %d samples (%dx%d, frame from %s)'
      % (tot / n, n, w, h, ppms[0].rsplit('/', 1)[-1]))
PY
