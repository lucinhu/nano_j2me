# Key scripts can hold a key

`--keys` drove a MIDlet with frame-stamped taps: a key went down at its frame
and up one frame later. That is the right shape for a menu, where one press is
one event, and the wrong shape for the movement in most games: they read
`getKeyStates()` and move only while a direction stays down. Verifying Doom's
gameplay therefore needed a host-side bridge (`state_bridge.c` plus a control
file, kept out of the tree under `.local-tools`) whose `press KEY [ms]` command
held a key in real time. Every title that needed real play had to repeat that
setup, and the replay could not be checked into a test.

## Syntax

Two more fields name the edges instead of a tap:

```
frame:key            # tap: down at `frame`, up one frame later (unchanged)
frame:key:down       # key down, and it STAYS down
frame:key:up         # key up, exactly once
```

Doom's menu entry and its first turn, as one script:

```
480:down:down,486:down:up,489:fire,520:left:down,740:left:up
```

The third field is why `:down` is not another `@` form: `down` is also the name
of the down-arrow key, so `5:down` must stay unambiguous. An unknown third
field (`100:fire:sideways`) rejects the whole script, the same way any other
malformed entry does.

## Implementation

`key_script` (src/runner/nj_run.c) gained an `edge[NJ_KEY_SCRIPT_MAX]` array:
zero for a tap, `NJ_KEY_SCRIPT_EDGE_DOWN` or `_UP` otherwise. Nothing about the
script's size or the runner's hot path changes beyond one byte per entry, and
the structure lives on the CLI's stack.

`key_script_parse()` splits the key token at its colon before the name lookup
and stores the edge. `key_script_tick()` sends `NJ_EV_KEY_UP` for an `:up`
entry, and the automatic-release pass skips every explicit edge. That skip is
the whole feature: a `:down` that the release pass did not skip would come up
one frame later, which is exactly the tap it was meant to replace, and an
`:up` would be followed by a second up.

## Tests

`tests/test_poke.c` observes the events the runner actually delivers, so the
event count is the assertion. The hold script
`40:down:down,60:fire,80:down:up` must deliver exactly four events (down, fire
down, fire up, down up); an auto-released hold would deliver a fifth, and a
double up a sixth. `10:left:up` must deliver exactly one release, and a bad
third field must be reported as a rejected script. test_poke reports 24 checks
with 0 failures (20 before).

Mutation check: dropping `ks->edge[ks->next_up] != 0` from the release pass
makes exactly two checks fail - "a held key stays down until its explicit up
edge" and "an explicit up edge sends exactly one release" - and restoring it
passes all 24.

## Real-game evidence, without the bridge

`毁灭战士3_10598` was replayed with the tracked CLI only. Menu entry is a
six-frame Down hold followed by a Fire tap; then the script holds Left:

```
build/nj2me --port headless --heap 16 --fps 30 --size 240x320 \
  --run-frames 800 --dump-every 20 --dump-frames out/ --keys \
  "480:down:down,486:down:up,489:fire:down,492:fire:up,520:left:down,740:left:up" \
  tests/games/毁灭战士3_10598.jar
```

Frames 460 and 480 are the menu; 500 is the corridor after New Game; dumps at
540, 560, 580 ... 760 all have different hashes, i.e. the view keeps turning
for as long as Left is held; 760, 780 and 800 are identical, i.e. the turn
stops when the script ends. `frames=800 paints=360 gfx=4324 tris=103371
m3g=40873 time=29.249s fps=27.4`, exit status 0. A forward variant
(`520:up:down,760:up:up`) walks out of the corridor into the next room and then
stops against a wall, which is the game's collision and not a lost key.

The mid-turn frame at `turn/frames/00700.png` shows the wall the player is
turning past, the crosshair and the ammo HUD. Evidence is under
`.local-tools/debug-runs/doom-heldkeys/`.

## Limits

- There is no key repeat: a title that expects repeated `keyPressed()` events
  while a key is held still needs several entries, or the game's own repeat.
- The edges are delivered exactly as written. An `:up` with no matching
  `:down` is one release, which is useful for releasing a key pressed in an
  earlier script but raises no error.
- Script parsing remains all-or-nothing: one bad entry ignores the rest, and
  the CLI says so.
