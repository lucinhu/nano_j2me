# 蹦球传说_7801: a manifest-driven Nokia gate, not a loader bug

`tests/games/蹦球传说_7801.jar` (Bounce Tales 2.0.3, MIDlet-Vendor Nokia) was
the one title the round-8 reference comparison reported as "we paint nothing,
the reference reaches its menu".  It turned out to be a second kind of platform
check, and the numbers below are why the first diagnosis looked like a resource
bug.

## Symptom

With the default device profile the title runs 94 frames, then stops:

```
[stop] nj_vm_stop code=0 ... frames=4
[stop]   at java/lang/System.exit pc=0
[stop]   at q.<clinit> pc=268
[stop]   at m.<clinit> pc=546
[stop]   at o.run pc=171
thrown: java/lang/RuntimeException at q.a(I[Ljava/lang/String;)Ljava/lang/String; pc=82: getResourceAsStream
run ended: the MIDlet called System.exit() (exit code 0)
```

(`[stop]` lines are from a temporary diagnostic that dumped the Java frames of
the thread that called `nj_vm_stop`; it has since been removed, the rest of the
log is stock.)

That `RuntimeException("getResourceAsStream")` is a red herring: `q.a` loads the
language table, and because the game had already asked the VM to stop, the
nested `new ByteArrayInputStream(...)` returned `NJ_ERR_STATE` and the native
call failed.  The language data itself (`/lang.xx`, 4738 bytes) is present and
readable.

## The gate

`q.<clinit>` reads `/META-INF/MANIFEST.MF` through `Class.getResourceAsStream`,
scans the lines for one starting with `Nokia-Platform:`, keeps what follows the
colon (`Nokia*`), splits it on `@`, and compares each part against
`System.getProperty("microedition.platform")` with `*` as a wildcard.  On no
match it calls `System.exit(0)` (pc 268).  The shipped manifest says exactly:

```
Nokia-Platform: Nokia*
```

The default `generic` profile answers the port's own identity, so the word
"Nokia" never matches and the game exits by itself.  The reference emulator
reaches the menu because its class transformer rewrites vendor literals such as
`"Nokia"` to a prefix of its own platform string — the same mechanism that lets
`1945空战英雄_6105` boot there.

## What the runtime does instead

Consistent with the documented policy (`src/core/nj_device.c`: the default
profile runs because the runtime works, not because a handset was imitated), the
title is verified as a Nokia handset:

```
build/nj2me --port headless --size 176x208 --heap 16 --fps 30 \
    --device nokia6230 --run-frames 1500 \
    --dump-frames .local-tools/debug-runs/bounce-r8/nokia2/frames/ \
    tests/games/蹦球传说_7801.jar
```

- 900 frames: the story screen ("One misty morning in Sky Bean Land..." with an
  `OK` softkey, frame 750 crop in `bounce-r8/nokia/frames/00750.ppm`).
- 1,500 frames with taps: actual gameplay — character, flowers, arrow signs,
  `0/30` egg counter and a 0:05 timer
  (`bounce-r8/nokia2/frames/01500.ppm`).
- 30.2 fps, exit 0; the only exception left is the game's own caught
  `RecordStoreNotFoundException` on a fresh RMS.
- Reference parity: FreeJ2ME reaches the same title screen/menu at 176x208
  (`.local-tools/debug-runs/refdiff2-r8/<hash>/ref/shot.png`).

## Limitations

Level completion and the save/continue path are not verified.  The old corpus
snapshot called this title `PASS (static picture)`: with the default profile it
does paint ~55 frames before exiting, which is why the sweep accepted it; the
device retry never ran because frames were presented.

## Files touched

None.  This round changed documentation only; the two diagnostic patches in
`src/core/nj_lib_object.c`/`nj_thread.c`/`nj_vm.c`/`nj_lib_midlet.c` were
reverted before the commit.
