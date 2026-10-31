#!/bin/bash
# The desktop client without a display.
#
# A bare `nj2me` in an SDL2 build opens the games library; SDL's dummy video
# driver runs that same window code with no X server, and the client has two
# test hooks for exactly this:  NJ2ME_CLIENT_FRAMES=N ends the session after N
# frames, and NJ2ME_CLIENT_SCREENSHOT=FILE writes the launcher frame as a PPM.
#
# What this proves, end to end:
#   * the library opens, scans a games directory and paints something (a frame
#     of pure background would have a handful of colours; real rows, Chinese
#     names and icons have many),
#   * --print-library agrees with what the scan found, including a bad jar,
#   * --client <jar> starts the game, and --run-frames ends the whole client
#     afterwards (the same seam a test needs to reach the window path at all),
#   * a client.conf written with display/heap/mute/network and map_ lines is
#     read by a direct run too, and the run still exits cleanly,
#   * a saved window = WxH opens the launcher at that size and survives exit,
#   * --client <jar> records the jar as recently played in client.conf, once,
#     which is what feeds the 最近玩过 section next time.
#
# The states a headless run cannot reach - a real drag-and-drop, a real click,
# a real window resize - are covered by tests/test_port_drop.c (drop policy)
# and tests/test_client_ui.c (the model's keys and hit-testing), and verified
# by hand with a display.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
GAMES="${1:-$ROOT/tests/games}"
JAR="${2:-$ROOT/tests/fixtures/alphademo.jar}"

[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 77; }
[ -d "$GAMES" ] || { echo "SKIP: no games directory at $GAMES"; exit 77; }
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 77; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Keep the client's settings and save data inside the work directory: a test
# must not write to the developer's real ~/.config or ~/.local.
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export XDG_CONFIG_HOME="$WORK/config"
export XDG_DATA_HOME="$WORK/data"

FAILS=0
say()  { printf '  %s\n' "$*"; }
ok()   { printf '  ok   %s\n' "$*"; }
bad()  { printf '  FAIL %s\n' "$*"; FAILS=$((FAILS + 1)); }

say "launcher:"
if NJ2ME_CLIENT_FRAMES=6 NJ2ME_CLIENT_SCREENSHOT="$WORK/launcher.ppm" \
        "$BIN" --client --games "$GAMES" >"$WORK/launcher.log" 2>&1; then
    ok "the library window ran and exited cleanly"
else
    bad "the library window failed; log follows"
    sed 's/^/    /' "$WORK/launcher.log"
fi
if [ -s "$WORK/launcher.ppm" ]; then
    ok "wrote a launcher frame"
else
    bad "no launcher frame was written"
fi
if [ -s "$WORK/launcher.ppm" ]; then
    if command -v python3 >/dev/null 2>&1; then
        if info=$(python3 - "$WORK/launcher.ppm" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
if not data.startswith(b'P6\n'):
    print('not a P6 PPM'); raise SystemExit(1)
parts = data.split(b'\n', 3)
w, h = (int(v) for v in parts[1].split())
px = parts[3]
assert w == 720 and h == 540, (w, h)
assert len(px) >= w * h * 3, len(px)
body = px[:w * h * 3]
colors = len(set(zip(body[0::3], body[1::3], body[2::3])))
assert colors > 20, colors
print('%dx%d, %d colours' % (w, h, colors))
PY
        ); then
            ok "the frame is $info"
        else
            bad "the launcher frame is not a plausible 720x540 picture"
        fi
    else
        say "skipped pixel checks (no python3)"
    fi
fi

say "the library window remembers a saved size:"
mkdir -p "$WORK/config/nano_j2me"
cat >"$WORK/config/nano_j2me/client.conf" <<CONF
games_dir = $GAMES
window = 880x660
CONF
if NJ2ME_CLIENT_FRAMES=6 NJ2ME_CLIENT_SCREENSHOT="$WORK/sized.ppm" \
        "$BIN" --client >"$WORK/sized.log" 2>&1; then
    ok "a saved window size starts the client"
else
    bad "a saved window size broke the client; log follows"
    sed 's/^/    /' "$WORK/sized.log"
fi
if command -v python3 >/dev/null 2>&1; then
    if info=$(python3 - "$WORK/sized.ppm" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
parts = data.split(b'\n', 3)
w, h = (int(v) for v in parts[1].split())
assert w == 880 and h == 660, (w, h)
print('%dx%d' % (w, h))
PY
    ); then
        ok "the frame is the saved $info"
    else
        bad "the launcher did not open at the saved size"
    fi
else
    say "skipped the size check (no python3)"
fi
grep -q "^window = 880x660$" "$WORK/config/nano_j2me/client.conf" \
    && ok "the size is still in client.conf after exit" \
    || bad "the saved window size disappeared"

say "print-library:"
if "$BIN" --print-library --games "$GAMES" >"$WORK/library.log" 2>&1; then
    ok "--print-library exited 0"
else
    bad "--print-library failed; log follows"
    sed 's/^/    /' "$WORK/library.log"
fi
grep -q "^library: $GAMES\$" "$WORK/library.log" && ok "named the games directory" \
    || bad "did not name the games directory"
grep -qE '^  ok   ' "$WORK/library.log" && ok "found runnable games" \
    || bad "found no runnable games"
grep -qE '^[0-9]+ games, [0-9]+ runnable$' "$WORK/library.log" \
    && ok "printed a summary" || bad "printed no summary"

say "a directory with a broken jar:"
mkdir -p "$WORK/bad"
cp "$ROOT/tests/fixtures/bogus_cd_sig.jar" "$WORK/bad/broken.jar" 2>/dev/null \
    || cp "$JAR" "$WORK/bad/broken.jar"
"$BIN" --print-library --games "$WORK/bad" >"$WORK/bad.log" 2>&1
if grep -qE '^  bad  ' "$WORK/bad.log"; then
    ok "a broken jar is listed with its reason"
else
    bad "a broken jar was not reported"
fi
if grep -qE '^1 games, 0 runnable$' "$WORK/bad.log"; then
    ok "the broken jar is not counted as runnable"
else
    bad "the runnable count is wrong for a broken jar"
fi


say "settings reaching a direct run:"
mkdir -p "$WORK/config/nano_j2me"
cat >"$WORK/config/nano_j2me/client.conf" <<CONF
# what the settings page writes, condensed: a rebind, a filter, a mute and a
# heap size, all of which the one-shot run has to accept.
games_dir = $GAMES
filter_linear = 1
fullscreen = 0
accel = 0
heap_mb = 16
mute = 1
network = 0
map_up = W
map_fire = J
CONF
if "$BIN" --port sdl2 "$JAR" --run-frames 4 --stats >"$WORK/direct.log" 2>&1; then
    ok "a direct run read client.conf and exited 0"
else
    bad "a direct run with client.conf failed; log follows"
    sed 's/^/    /' "$WORK/direct.log"
fi
grep -q "nano_j2me: exit status 0" "$WORK/direct.log"     && ok "the settings-bearing run ended cleanly"     || bad "the settings-bearing run did not end cleanly"
# The client must not have rewritten the file behind the user's back.
grep -q "^map_up = W$" "$WORK/config/nano_j2me/client.conf" \
    && ok "the key binding is still in the file" \
    || bad "the key binding disappeared"

say "auto-starting a game, then coming back:"
if "$BIN" --client "$JAR" --run-frames 4 --stats >"$WORK/game.log" 2>&1; then
    ok "a run under --client --run-frames exited 0"
else
    bad "the scripted client run failed; log follows"
    sed 's/^/    /' "$WORK/game.log"
fi
grep -q "nano_j2me: exit status 0" "$WORK/game.log" \
    && ok "the game itself ended cleanly" || bad "the game's exit status was not 0"

# A launch through the client is a recent game, remembered for the next window;
# running the same jar again must move it to the front, not append it twice.
say "the last game played:"
RECENT_CONF="$WORK/config/nano_j2me/client.conf"
if grep -q "^recent = $JAR$" "$RECENT_CONF"; then
    ok "the client recorded the jar it ran"
else
    bad "the client did not record the jar it ran"
fi
"$BIN" --client "$JAR" --run-frames 4 >"$WORK/game2.log" 2>&1
if grep -q "^recent = $JAR$" "$RECENT_CONF"; then
    ok "the recent list survives another launch"
else
    bad "the recent list lost the jar on a second launch"
fi
if [ "$(grep -c "^recent = $JAR$" "$RECENT_CONF")" -eq 1 ]; then
    ok "the same jar is listed once"
else
    bad "the same jar is listed more than once"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '\n%d check(s) failed\n' "$FAILS"
    sed 's/^/  | /' "$WORK/game.log" 2>/dev/null
    exit 1
fi
printf '\nall checks passed\n'
