# Malformed-JAR fuzzing

The class fuzzer (`tools/fuzz_classfile.py`) exercises the class-file parser.
This note covers the container around it: a damaged JAR shipped by a bad
packer, a broken upload or a truncated download must be rejected by the loader
as a handled error — never a crash and never a hang.

## Tool

`tools/fuzz_jar.py [BIN [CASES [MUTATIONS [SEED]]]]`

It mutates a fixture JAR byte-for-byte and runs the headless CLI on each case
for 20 frames:

- `byte` — flip one byte at a random offset;
- `truncate` — cut the file at a random offset;
- `append` — append random bytes (stale offsets, appended garbage);
- `localhdr` — corrupt a local file header signature;
- `cd` — corrupt the central directory signature;
- `eocd` — corrupt the end-of-central-directory signature;
- `manifest` — flip a byte inside `META-INF/MANIFEST.MF`;
- `resource` — flip a byte inside a resource entry (the fixture carries two
  PNGs).

A case is a **CRASH** when the process dies from a signal and a **HANG** when it
exceeds the budget (the unmutated fixture's time × 40, at least two seconds, so
the bound follows the machine).  A clean non-zero exit is fine: rejecting a
malformed JAR is the correct behaviour, only crashing or hanging is a defect.

Because a second mutation may no longer find the region the first one moved or
truncated, failed follow-up mutations fall back to trimming the buffer; the
result is still a malformed JAR.

`gamedemo.jar` is the preferred fixture because it has multiple classes *and*
resources, so the manifest and resource mutations have room to work.

## Evidence, 2026-11-15

Against `/var/tmp/nj2me-goal13-build/nj2me`:

- 300 cases, one mutation, seed 5 → **clean 300 / crashed 0 / HUNG 0**;
- 300 cases, two mutations, seed 11 → **clean 300 / crashed 0 / HUNG 0**.
- 800 cases, two mutations, seed 23 → **clean 800 / crashed 0 / HUNG 0**
  (round 49).

Logs: `.local-tools/debug-runs/fuzz-jar-r46.log` (two mutations) and
`.local-tools/debug-runs/fuzz-jar-r46-single.log` (one mutation), both
gitignored.

The ctest gate `fuzz_jar` (`python3 tools/fuzz_jar.py <cli> 60 2`, TIMEOUT 300)
covers the modes in about 0.2 s; the full serial suite is now **187/187 passed
in 339.07 s** (`/tmp/r46-ctest.log`).

## What is still not covered

- a JAR with several *independent* defects at once (each case mutates one
  region);
- mutations designed against the ZIP reader's specific fields (the `cd`/`eocd`
  kinds only hit the signatures; count/offset fields of a valid-looking
  central directory are not perturbed);
- a real-world bad archive larger than the small fixtures (the reader's
  behaviour on huge entry counts is bounded by the same loops, but the fuzzed
  sizes here are tiny).
