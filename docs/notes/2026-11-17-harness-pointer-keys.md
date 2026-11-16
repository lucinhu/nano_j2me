# Mock frontend: pointer taps in key scripts were silently dropped

2026-11-17 (round 80)

## Symptom

`tests/test_libretro_frontend.c` gained `pressed@X,Y` key-script entries in
round 75 so the whole-corpus visual probe could tap menus (the curated
`tools/corpus_keys/*.txt` scripts use them for `丧尸危机2` and
`凡人修仙传`). Running the harness by hand on such a script printed:

```
[info] key script: 2 entr(ies) use keys without a default joypad binding and were skipped
```

and a script with *only* pointer entries was rejected outright:

```
error: --keys '/tmp/k1.txt' has no usable frame:key entries
```

So pointer taps never reached the core in any harness-driven probe; the
per-title scripts worked only through their joypad entries.

## Root cause

The key-script tokenizer split the file on **commas** (and whitespace) before
parsing each `frame:key` pair. A pointer tap is written `60:pressed@40,50`,
which the splitter cut into `60:pressed@40` (the `sscanf("%d,%d")` then failed)
and a stray `50`. The parser counted the first fragment as an unusable key
name and dropped the tap.

## Fix

`load_key_script()` now treats whitespace as the field delimiter and decides
internally where an entry ends: a comma starts a new entry only when it is
followed by a number and a colon (`...,120:fire`). The comma inside
`pressed@X,Y` coordinates is followed by a number but no colon, so the tap
stays intact. Both forms, and a mixed field
(`30:fire,60:pressed@40,50`), now parse:

```
$ ./test_libretro_frontend --core ... --jar tests/fixtures/loopdemo.jar \
    --frames 90 --keys /tmp/k2.txt
[summary] checks=45 failed=0 skipped=0
```

Verified with three script shapes (pointer only, mixed field, one entry per
line); no `key script: ... skipped` line and no `error: --keys` remains.

## Impact

Re-ran the 600-frame whole-corpus visual probe with the fixed harness
(`.local-tools/debug-runs/libretro-visual-r80/`). The probe numbers in
`docs/LIBRETRO.md` and the submission readiness note were refreshed after the
run; the instruction stands: when a curated script contains `pressed@`, check
that the harness log does not report skipped entries before trusting the
probe.
