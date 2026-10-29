# Android: automatic Java heap

The launcher used to offer a fixed list of heap sizes — 4, 8, 16, 24, 32 and
64 MB — and started every game with the smallest useful one, 8 MB.  A phone
with 2 GB of RAM and one with 12 GB got the same answer, and the player had to
know in advance that a 3D title needs more.

A game that has not chosen a size now runs with **automatic** heap, resolved
when the MIDlet is launched from the phone's total RAM:

```
heap = clamp(total_ram / 16, 32 MB, 128 MB), rounded down to 8 MB
```

| Phone RAM | Automatic heap |
|---|---|
| unknown / very small | 32 MB |
| 512 MB | 32 MB |
| 768 MB | 48 MB |
| 1 GB | 64 MB |
| 1.5 GB | 96 MB |
| 2 GB and above | 128 MB (ceiling) |

The runtime's Java heap is one arena the port allocates and clears at VM
creation, so "as much as fits" is the wrong rule: one sixteenth of RAM leaves
the rest of the phone alone, the 32 MB floor still starts a 3D title, and the
128 MB ceiling stops a single MIDlet holding memory Android wants back.  64 MB
is what Minecraft PE needed on the desktop port here, and a 1 GB phone gets
exactly that.

## Where it lives

* [HeapPlan.java](../../src/port/android/java/com/lucinhu/nj2me/HeapPlan.java) —
  the policy alone, no Android types, so it can be tested off the device.
* `JarStore.autoHeapMB(Context)` reads `ActivityManager.MemoryInfo.totalMem`
  and delegates to it; `JarStore.heapText()` is what the settings rows show
  ("自动（约 96 MB）").
* `GameActivity` resolves a stored `heap=0` at launch, so moving the same games
  folder to a bigger phone raises the heap without editing sidecars.

## Sidecars and migration

`heap=0` means automatic.  When the player picks a size in the dialog, the
sidecar also gets `heapset=1`, so a deliberate 8 MB can be told apart from the
old default.  A settings file written before this change has `heap=8` and no
marker: that was the old default, so it is read as automatic.  Any other
number (4, 16, 24, 32, 64) was a real choice and is kept.  The fixed sizes stay
available for a title that reports `OutOfMemoryError` anyway, and 128 MB was
added to that list for large 3D games.

## Verification

* [HeapPlanTest.java](../../tests/android/com/lucinhu/nj2me/HeapPlanTest.java)
  pins the table above, the 8 MB grid, the floors and ceilings, and both
  sidecar cases (legacy default moves to automatic, explicit choice stays).
  It runs in the existing off-device harness:
  `tools/check_android_java.sh` — 16 checks, and the whole harness is green.
* Every Android Java file was recompiled against the SDK's `android.jar`
  (`javac -source 8 -target 8 -bootclasspath …/android.jar`) after the change.
* Both APKs were rebuilt and signed against the project's debug key - arm64
  (891 036 bytes) and x86_64 (891 033 bytes) - from a clean `git worktree` at
  the commit, because the working tree also carried unrelated, in-flight C
  changes that do not compile. The x86_64 one was installed on the API 26
  x86_64 emulator (1.46 GB RAM reported):
  * the game settings sheet shows "Java 堆 · 自动（约 88 MB）";
  * the chooser lists "自动（约 88 MB）" first and selected, followed by the
    fixed sizes (4/8/16/24/32/64/128 MB);
  * launching the game logs `heap: automatic, 88 MB for this phone`;
  * picking 16 MB writes `heap=16` + `heapset=1` to
    `files/jars/<game>.jar.cfg`, and choosing 自动 again writes `heap=0` with
    no marker.
  Screenshots: `.local-tools/android-autoheap-r01/`.

## Limits

* Automatic is a heuristic, not a measurement of the running MIDlet: a very
  large game on a small phone can still run out and needs an explicit larger
  size (the dialog is the answer, and the row shows the number in use).
* The value is resolved per launch, so it changes if the phone's RAM report
  changes; the sidecar keeps only "automatic".
