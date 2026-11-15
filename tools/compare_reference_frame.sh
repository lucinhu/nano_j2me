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
# pixel certificate.  Exits 77 (skip) when PIL or the reference is missing;
# the optional seventh argument is the MAE threshold (default 12/255) and the
# eighth the pacing fps (0 = unthrottled).  Set KEEP=<dir> to keep our frame,
# the reference shot and a side-by-side composite when the check fails.
# Set SAMPLES=<n> (default 1) to compare n of our frames spread STEP frames
# apart (default STEP=<frame>/2) and report the best match: a single frame
# number maps to a different point of each runtime's timeline, so a window of
# candidates is what actually makes a high MAE meaningful.
set -u

bin=${1:-}; jar=${2:-}; ref=${3:-}; frame=${4:-300}; w=${5:-240}; h=${6:-320}
thr=${7:-12}; fps=${8:-0}
samples=${SAMPLES:-1}; step=${STEP:-$((frame / 2))}
[ "$step" -gt 0 ] 2>/dev/null || step=$frame
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
keep=${KEEP:-}
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/ours" "$tmp/refdir"

last=$((frame + (samples - 1) * step))
"$bin" --port headless --size "${w}x${h}" --heap 32 --fps "$fps" \
       --run-frames "$last" --dump-frames "$tmp/ours/" --dump-every "$step" \
       "$jar" >"$tmp/ours.log" 2>&1 || { echo "FAIL: our run exited $?"; exit 1; }

if [ "$fps" -gt 0 ] 2>/dev/null; then
    hold=$((3000 + frame * 1000 / fps))
else
    hold=7000
fi
printf 'wait 3000\npress 5 200\nwait %s\nshot %s/ref.png\nquit\n' "$hold" "$tmp" |
    "$ref" "$jar" "$tmp/refdir" "$w" "$h" >"$tmp/ref.log" 2>&1 ||
    { echo "FAIL: reference run exited $?"; exit 1; }

python3 - "$tmp" "$thr" "$keep" <<'PY'
import glob, sys
d = sys.argv[1]
thr = float(sys.argv[2])
ppms = sorted(glob.glob(d + '/ours/*.ppm'))
pngs = sorted(glob.glob(d + '/**/*.png', recursive=True))
if not ppms:
    print('FAIL: our run produced no frame'); sys.exit(1)
if not pngs:
    print('FAIL: the reference produced no screenshot'); sys.exit(1)
from PIL import Image
ref = Image.open(pngs[0]).convert('RGB')
best = None
for path in ppms:
    a = Image.open(path).convert('RGB')
    if a.size != ref.size:
        print('FAIL: sizes differ', a.size, ref.size); sys.exit(1)
    pa, pb = a.load(), ref.load()
    w, h = a.size
    tot = n = 0
    for y in range(h):
        for x in range(w):
            for c in range(3):
                tot += abs(pa[x, y][c] - pb[x, y][c]); n += 1
    mae = tot / n
    if best is None or mae < best[0]:
        best = (mae, a, path)
mae, a, path = best
label = path.rsplit('/', 1)[-1]
suffix = ' (best of %d frames)' % len(ppms) if len(ppms) > 1 else ''
print('MAE %.2f / 255 over %d samples (%dx%d, frame from %s)%s'
      % (mae, n, w, h, label, suffix))
if mae > thr:
    print('FAIL: MAE %.2f exceeds the %.2f threshold' % (mae, thr))
    keep = sys.argv[3] if len(sys.argv) > 3 else ''
    if keep:
        import os, shutil
        os.makedirs(keep, exist_ok=True)
        shutil.copy(path, os.path.join(keep, 'ours.ppm'))
        shutil.copy(pngs[0], os.path.join(keep, 'ref.png'))
        c = Image.new('RGB', (w * 2 + 8, h), (40, 40, 40))
        c.paste(a, (0, 0)); c.paste(ref, (w + 8, 0))
        c.save(os.path.join(keep, 'side-by-side.png'))
        print('kept artifacts in ' + keep)
    sys.exit(1)
PY
