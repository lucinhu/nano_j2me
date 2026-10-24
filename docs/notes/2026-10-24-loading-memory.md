# Bounded-memory loading of NFS Hot Pursuit and The Run

The original JARs (SHA-256 prefixes `77d10a56e51a` and `92c5f29e1d66`)
failed before Java startup on a constrained 32-bit port at 240x320 with a
6 MiB Java heap. This reproduced with JIT disabled, both with normal RMS
and fresh temporary RMS, including after a cold restart. Updating to
`7798440` alone did not resolve it.

## Archive allocation

The port supplied each file's length, but the runner ignored it and grew a
64 KiB allocation by doubling. Both titles, at 1,249,241 and 1,071,450 bytes,
therefore retained a 2 MiB buffer. Old and new buffers coexisted during each
copy. At VM creation there were 7,750,568 free host bytes, but the largest
block was 5,636,096 bytes; the 6,291,472-byte heap allocation failed.

The runner now starts with the port's length hint and checks EOF before
growing. It retains short-read and unknown-length support, accepts a file
that grows beyond its hint, checks growth overflow, and releases the buffer
when an empty file is rejected. The JAR bytes are unchanged.

## M3G temporary object tables

Fixing the archive allocation exposed the next failure: Hot Pursuit stopped
after its sound prompt, and The Run stopped after car selection. Both lost
their loader TimerTask after failed model loading.

Every M3G load reserved 4096 object descriptors, even for a single texture.
On this ABI that is 688,128 bytes before allocation headers. Nested external
texture loads kept more than one table alive. One captured failure requested
688,144 bytes with 1,027,144 bytes free but only 688,128 in the largest block;
another failed a 66,592-byte payload allocation during object decoding.

The loader now counts objects before allocating the table and retains the
4096-object limit. The first pass owns only temporary section scratch; the
second pass decodes and copies object payloads as before. Compressed sections
are consequently inflated twice during loading, a CPU/memory tradeoff.

The two archives contain 36 M3G resources each. Hot Pursuit has 13 one-object
texture resources, The Run has 16: their tables now need 168 bytes each on
the same ABI. Their largest tables contain 1355 and 1689 objects, requiring
227,640 and 283,752 bytes respectively. Object references, geometry and
texture precision are unchanged.

## Regression checks

- `test_run_archive` drives the public runner with a bounded allocator and
  checks exact-size files, boundary EOF, short reads, unknown lengths, stale
  length hints, read errors, failed growth and empty-file cleanup.
- `test_m3g_load_memory` loads one-object and 256-object containers under a
  128 KiB per-allocation limit and checks empty/over-limit rejection and full
  teardown cleanup.
- Both regressions fail against the corresponding original source from
  `7798440`. The old empty-file path additionally leaks a 64 KiB buffer.
- Both new tests, `test_m3g` and `test_lcdui` pass under ASan/UBSan.

## Java heap budget

Those fixes allow startup and model decoding, but do not make a 6 MiB Java
heap sufficient for these scenes. With both fixes, The Run reached
6,253,376 live bytes and failed in `Transformable.setTranslation`; its
TimerTask exited. Hot Pursuit also exhausted its Java heap while creating
an image. These are separate from host staging allocations.

The integration now leaves firmware code and static assets in flash instead
of copying the entire image into external RAM, and assigns 10 MiB to the
Java heap. The interpreter/JIT hot functions retain their internal-RAM
placement. This is a port configuration change, not a larger upstream
runtime default. Tests below use the additional keyframe update in
`527d3fd` as well as these loader fixes.

## Device validation

At 240x320, JIT disabled, with sound enabled and fresh temporary RMS:

- **The Run:** car selection, SPRINT loading, then a textured 3D city race.
  Captures show 6% progress at 125 MPH and 28% at 81 MPH after further
  driving and steering. The final live Java heap was 7,000,304 bytes,
  already larger than the old 6 MiB budget. Ten one-second FPS-overlay
  queries during driving averaged 4.21 (4.0–4.4); these are short rolling
  changed-image rates, not a claim of smooth gameplay or a JIT comparison.
- **Hot Pursuit:** sound prompt, tutorial choice, loaded tutorial track,
  advancing from tutorial 1/11 to 2/11. A later capture shows the road,
  mountains and player car at 38 MPH after brake input. MIDI diagnostics
  include nonzero synthesized samples.

Neither title reported host allocation failure or Java OutOfMemoryError in
these final runs. Both returned to the launcher with status 0, identical
before/after free external RAM, and no live JIT allocations. Hot Pursuit
still logged a RuntimeException from `destroyApp()` during manual stop;
save round trips have not been verified. Its sound-enabled run had PCM
output and zero driver write errors, with one underrun across a session
that included screenshot pauses. This is not an audio-quality benchmark.

The original JARs and game partition were preserved, and normal RMS was
not cleared; the app image alone was updated. This validates startup and
short play, not complete races or every menu path.

The firmware image SHA-256 was
`1f29097f2797c82ca9e7aae13e3d27da4f05f2a287612ac0a0f23d4b3bde5ab5`.
The four ASan/UBSan tests above were repeated after merging `527d3fd` and
passed.

A follow-up 2D Bounce Tales session with JIT enabled executed 8,269,843
native bytecodes with zero JIT faults and released all code on exit. Both
NFS titles also completed a 300-frame startup using their normal RMS
folders. All five final-image runs returned status 0 and fully recovered
external RAM. The 2D session is a functional regression, not a matched
performance comparison for the changed memory layout.
