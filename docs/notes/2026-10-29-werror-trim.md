# The -Werror build stopped building, and the warning was a real rotation bug

`-DNJ_WERROR=ON` is a documented CMake option ("Treat warnings as errors") and
`docs/notes/2026-10-21-trim-builds.md` records a matrix of trimmed
configurations that all build with zero warnings.  Neither claim held any
more:

    cmake -S . -B /tmp/w -DNJ_WERROR=ON -DNJ_BUILD_TESTS=OFF ...
    src/midp/nj_lib_mascot.c:605:18: error: 建议在'>>'的操作数中出现的'+'前后加上括号
                                     [-Werror=parentheses]

so the *default* configuration did not build under the option at all, and
`-DNJ_ENABLE_VIDEO=0` failed twice more:

    src/midp/nj_lib_media.c:115:19: error: 'av_mem_new' defined but not used
    src/midp/nj_idct.h:20:13:     error: 'nj_idct8x8' defined but not used

## The warning was a defect, not a style problem

The three operators are the diagonal terms of an axis-angle rotation in
`AffineTrans.setRotation(axis, angle)`:

```c
m[0]  = cos_a + (((x * x + 2048) >> 12) * nc + 2048) >> 12;   /* before */
m[0]  = cos_a + ((((x * x + 2048) >> 12) * nc + 2048) >> 12); /* after  */
```

`>>` binds looser than `+`, so the shift also divided the cosine.  For a
60-degree rotation about x (`cos = 2048`, `nc = 2048`, unit axis 4096) the old
expression gives

    m00 = (2048 + 8,390,656) >> 12 = 2049     (0.5, should be 1.0)
    m11 = (2048 +     2,048) >> 12 =    1     (should be 2048)

i.e. a "rotation" that shrinks the model to half size.  The single-axis forms
(`rotationX/Y/Z`) were already correct, so the bug only appeared for titles
that use the general `setRotation(Vector3D, int)` overload.

Regression: `tests/test_mascot.c` (`r7_math`) now sets the matrix to a known
translation, calls `rotationX(60deg)` and then `setRotation(unit-x, 60deg)`,
and requires the two matrices to agree entry for entry plus `m00 == ONE`.
With the old precedence restored, exactly two checks fail; with the fix,
`test_mascot` is 242 checks / 0 failures.

## Trimmed configurations

Three units carried code the trimmed build could not use:

| unit | state | warning |
| --- | --- | --- |
| `nj_lib_media.c` | `NJ_ENABLE_VIDEO=0` | `av_mem_new` is only created by the video Player; its definition now sits inside `#if NJ_ENABLE_VIDEO` |
| `nj_h263.c` | `NJ_ENABLE_H263=0` | `nj_idct.h` defines static tables; the include moved inside the `#if NJ_ENABLE_H263` body |
| `nj_m4v.c` | `NJ_ENABLE_M4V=0` | same for `nj_vlc_intra.h` / `nj_vlc_inter.h`, now inside `#if NJ_ENABLE_M4V` |

## The gate that should have caught all of this

`tools/check_codec_trim.sh` compiles the affected units in both switch states,
but it compiled *without* `-Wall -Wextra`, so a warning was never an error
there.  It now compiles with `-Wall -Wextra -Werror -Wno-unused-parameter`
and:

* drops any base `-D` the caller names itself.  The old form passed
  `-DNJ_ENABLE_MP3=1` and `-DNJ_ENABLE_MP3=0` on one command line; without
  `-Werror` that was a silent redefinition, and the "off" variant was decided
  by whichever `-D` GCC applied last rather than by the flag under test.
* checks the units that have no trim unit of their own: `nj_h263.c` with
  `NJ_ENABLE_H263=0`, `nj_m4v.c` with `NJ_ENABLE_M4V=0`, `nj_lib_mascot.c`
  in its default and `NJ_ENABLE_MASCOT=0` shapes, and the existing
  `nj_lib_media.c` combinations.

The gate is ctest #47, so the strengthened version runs in the normal suite.
A/B: reverting any one of the four fixes fails exactly its own check
(`FAIL: ... does not compile with -DNJ_ENABLE_VIDEO=0` / `with NJ_ENABLE_H263=0`
/ `with NJ_ENABLE_M4V=0` / `with` no flags); restored, the script ends with
`OK: every decoder trim removes the code it claims to`.

## Validation

Eight configurations, each configured with `-DNJ_WERROR=ON
-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF -DNJ_BUILD_SDL2=OFF
-DNJ_BUILD_EMBEDDED_PORTS=OFF` and built from scratch, finish with **zero
warnings**: default, `VIDEO=0`, `AMR=0`, `MP3=0`, `MASCOT=0`, `M3G=0`, the
"lean" set and the "minimal" set from the trim note.  Full `ctest`
**124/124**; `test_mascot` 242 checks / 0 failures.
