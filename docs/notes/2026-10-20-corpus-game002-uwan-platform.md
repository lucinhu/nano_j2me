# 2026-10-20 - Corpus game-002 (1945空战英雄): Nokia-gated Uwan SDK, not a VM bug

## Symptom

`tests/games/1945空战英雄_6105.jar` exits 1 during corpus runs:
`cannot instantiate ExtendTorpedoMidlet: java/lang/IllegalAccessException`.

## Root cause

`com.uwan.UwanMIDlet`'s public constructor calls `c()`, whose `d()` does:

    p = System.getProperty("microedition.platform");
    return p.length() >= 5 && p.substring(0, 5).equals("Nokia");

and `c()` throws an empty-message `java.lang.IllegalAccessException` when
`d()` is false (exception table catches everything inside `d()` and returns
false). This jar is a Nokia-channel release: on the default `generic`
profile (platform `nano_j2me`) the check legitimately fails. The manifest is
valid; the class flags are `ACC_PUBLIC|ACC_SUPER`; nothing is wrong with
`Class.newInstance` — the exception is thrown by the game's own bytecode.

## Parity against local FreeJ2ME-Plus (`.local-tools/freej2me-plus`)

| run | result |
|---|---|
| FreeJ2ME default (`compatoverrideplatchecks:on`) | boots: mission menu with "NOKIA" header (screenshot) |
| FreeJ2ME `compatoverrideplatchecks:off` | **all-white frame** — fails too |
| nano default (`--device generic`) | IAE, exit 1 |
| nano `--device nokia6230` (platform `Nokia6230/05.50`) | **boots, exit 0**, 900f, RMS not-found handled by the game |

FreeJ2ME only passes via its bytecode transformer: once a
`"microedition.platform"` literal has been seen, a later `"Nokia"` literal is
rewritten to the prefix of `"FreeJ2ME-Plus, ..."` of the same length, so
`substring(0,5).equals(...)` matches its own platform string
(`MIDletLoader.java` visitLdcInsn + `knownModelStrings`). With the override
off it fails exactly like nano does.

## Decision

No runtime change. The default `generic` profile is deliberately not a
handset imitation (`src/core/nj_device.c`: "a title that runs here runs
because the runtime works and not because a handset was imitated"), and nano
already ships the opt-in `--device nokia6230` profile that makes such
identity-gated titles boot — verified exit 0 above. Adding default Nokia
spoofing (or FreeJ2ME-style literal rewriting) would trade that design for
one corpus entry out of 395.

Evidence: `.local-tools/debug-runs/corpus-20261017/game-002-{fj2me,fj2me-nooverride,nokia}/`.
