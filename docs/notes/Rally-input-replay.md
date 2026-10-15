# Rally 3D input replay validation

The original `3D拉力赛-进化汉化版_6366.jar` remains unchanged, SHA-256
`c870cb2412322b4f1f83a3ab219ef228224416d39826892838022587848c01fa`.
Replays use isolated RMS directories, a 240x320 display, 16 MB heap and 30 FPS.

## Restart and driving evidence

The `rally-newtimer-015` replay contains 416 input events and runs for 3,650
frames in headless and SDL2. Both exit normally at 30.1 FPS; measured durations
are 121.062 and 121.234 seconds respectively. The replay pauses, selects restart,
then applies acceleration and left/right inputs.

Headless samples at frames 2,600 through 3,600, every 100 frames, have eleven
distinct pixel hashes. Frame 3,300 shows 76 km/h and a race time of 23.47 seconds;
frame 3,600 shows 7 km/h at 33.42 seconds, with the car facing the fence after
steering. This establishes continuing simulation after restart. It does not
establish handling accuracy or completion of the stage.

SDL2 captures confirm the pause menu and the changed final car position. Three
intermediate captures (`after-restart`, `accelerating`, `left`) are identical,
showing 70 km/h at 10.81 seconds. Their repeated pixels do not establish smooth
presentation, even though the run records 3,650 paints. The cause of the repeated
X11 captures remains unresolved.

Evidence is under `.local-tools/debug-runs/rally-newtimer-015/`, including
`keys.txt`, both runtime logs, headless frames and SDL2 captures.

## Reference limitations

FreeJ2ME-Plus `baca76a4dda6f30abac67fb4a3db009eed05f91c` also reports the
original game's stale Timer callback:

```
Exception in thread "Timer-2" java.lang.NullPointerException:
Cannot invoke "a.repaint()" because "Rally.f" is null
at b.run(Unknown Source)
```

The isolated `rally-newtimer-ref-015` navigation ended in menus, and the 30 FPS
`rally-newtimer-ref-016` run remained on the championship loading screen. Neither
is successful reference evidence for restarting a race. The game's Timer errors
remain visible; no Timer restart or exception suppression was added.

## Input parser correction

While preparing the replay, an earlier script contained 566 events. The runner
previously parsed the first 512 and silently omitted the remainder. Another
failure path reported that the whole script was ignored but retained its valid
prefix, allowing unintended input to reach the game.

`key_script_parse` now rejects an event beyond its 512-entry capacity after
skipping separators and comments. Exactly 512 events followed by comments remain
valid. The caller clears all parsed input on failure, including when the port has
no logging callback, and its diagnostic explicitly states the event limit.

The existing gameplay test verifies rejection by comparing actual frame hashes
against a deterministic no-input run. It covers malformed suffixes, a valid full
script with trailing comments, overflow, and overflow without a logger. Restoring
the old parser produces four failures; the correction passes all 16 checks.
The complete CTest suite passes 79/79 cases (43.83 seconds).
