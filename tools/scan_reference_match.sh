#!/bin/sh
# Find the frame of ours that shows the same screen as the reference's
# screenshot.  Frame numbers do not line up between the two runtimes (loading
# and menu timing differ), so a comparison has to match screens, not indices.
#
#   tools/scan_reference_match.sh <nj2me> <jar> <run-freej2me-headless.sh>
#                                 [last-frame] [step] [w h]
#
# Prints the three closest frames by mean absolute error and exits 77 when the
# reference or PIL is missing.
set -u

bin=${1:-}; jar=${2:-}; ref=${3:-}; last=${4:-900}; step=${5:-30}; w=${6:-240}; h=${7:-320}
keys=${8:-}
if [ -z "$bin" ] || [ -z "$jar" ] || [ -z "$ref" ] || [ ! -x "$bin" ] || [ ! -f "$jar" ]; then
    echo "usage: $0 <nj2me> <jar> <run-freej2me-headless.sh> [last] [step] [w h]" >&2
    exit 2
fi
[ -x "$ref" ] || { echo "SKIP: reference runner not present"; exit 77; }
python3 -c 'import PIL' 2>/dev/null || { echo "SKIP: python3 has no PIL"; exit 77; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/njscan.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/ours"

printf 'wait 3000\npress 5 200\nwait 7000\nshot %s/ref.png\nquit\n' "$tmp" |
    "$ref" "$jar" "$tmp/refdir" "$w" "$h" >"$tmp/ref.log" 2>&1 ||
    { echo "FAIL: reference run exited $?"; exit 1; }
[ -s "$tmp/ref.png" ] || { echo "FAIL: the reference produced no screenshot"; exit 1; }

if [ -n "$keys" ]; then
    "$bin" --port headless --size "${w}x${h}" --heap 32 --fps 0 \
           --run-frames "$last" --dump-frames "$tmp/ours/" --dump-every "$step" \
           --keys "@$keys" "$jar" >"$tmp/ours.log" 2>&1 ||
        { echo "FAIL: our run exited $?"; exit 1; }
else
    "$bin" --port headless --size "${w}x${h}" --heap 32 --fps 0 \
           --run-frames "$last" --dump-frames "$tmp/ours/" --dump-every "$step" \
           "$jar" >"$tmp/ours.log" 2>&1 ||
        { echo "FAIL: our run exited $?"; exit 1; }
fi

python3 - "$tmp" <<'PY'
import glob, sys
d = sys.argv[1]
from PIL import Image
ref = Image.open(d + '/ref.png').convert('RGB')
rp = ref.load(); w, h = ref.size
best = []
for p in sorted(glob.glob(d + '/ours/*.ppm')):
    im = Image.open(p).convert('RGB')
    if im.size != (w, h):
        continue
    ip = im.load(); tot = n = 0
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            for c in range(3):
                tot += abs(ip[x, y][c] - rp[x, y][c]); n += 1
    best.append((tot / n, p.rsplit('/', 1)[-1]))
best.sort()
if not best:
    print('FAIL: no frames to compare'); sys.exit(1)
for mae, name in best[:3]:
    print('MAE %.2f  %s' % (mae, name))
PY
