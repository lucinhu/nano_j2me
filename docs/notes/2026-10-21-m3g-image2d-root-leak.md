# The ref root an Image2D never let go

The VM snapshot support (`--save-state` / `--load-state`) refuses to save while
a temporary ref root is held: a root is a C-side reference with no home in the
blob, so a snapshot taken with one live could not be resumed correctly.  That
refusal is correct, but for one title it never went away.

## What was seen

`孤岛余生2时间之谜汉化版_7371.jar` boots to its opening room, and
`--save-state ... --save-state-frame 6100` answered

    cannot save state to .../room.njs (-8); the run continues

and `-8` is `NJ_ERR_STATE`.  A temporary diagnostic build that named the
refusal reason printed `can_save: temp root held`, and a second one that
recorded the caller of every `nj_root()` showed the stack two entries deep,
both from the same place:

    root[0] pc=0x48238b  ->  m3g_img2d_init_obj  (src/midp/nj_lib_m3g.c)
    root[1] pc=0x48238b

The leak first appeared at frame 1109, one frame after the title's loader
started building its textures.

## The leak

`Image2D(int format, Object image)` - the form a title uses when it has loaded
a PNG itself and wants it as a texture - converts the LCDUI image's ARGB
`imgPixels` into the RGBA byte array the M3G sampler reads.  That array is
allocated and rooted while it is filled:

```c
nj_ref dst = nj_array_new(vm, "B", n * 4u);
...
nj_root(vm, dst);          /* protect it while the loop runs */
...
nj_field_write_ref(vm, self, f, dst);
```

The root was never released.  Each construction therefore pinned one byte
array for the life of the VM, and - because a snapshot cannot represent a
temporary root - it also made every snapshot of a title that used this
constructor fail.  The M3G side of the engine had no tests for it because
nothing in the corpus asserted snapshotting.

## The fix

The conversion takes a root mark before the push and releases it after the
array has been stored into the object's `_nj_pixels` field: once the field
holds it the collector can see it, and there is no reason to keep it in the
root stack.

## Verification

* `tests/test_m3g.c`: the existing `Image2D(int, Object)` block now records
  `nj_root_mark()` before constructing and asserts it is unchanged afterwards.
  Removing the release fails exactly one assertion
  (`Image2D(format, Image) leaves no ref root behind`); with the fix
  `test_m3g` reports 309 checks, 0 failures.
* Real game: a 6,200-frame boot of the title (205 s of its own clock at
  30.2 fps) now writes `room.njs` (15.5 MB); a fresh process resumed it with
  `resumed GameMIDletSTRANDED2 from .../room.njs` and rendered 400 frames at
  29.9 fps.  Before the fix the same boot produced no snapshot at all.
* 87/87 ctest pass on the clean build.

Evidence: `.local-tools/debug-runs/stranded-r5/diag5/` (refusal reason, saved
snapshot, resume run).
