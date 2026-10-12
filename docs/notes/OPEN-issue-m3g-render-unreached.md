# OPEN: no title reaches `Graphics3D.render()`, and the instrument lied about it

## What was believed

`--stats` has printed an `m3g=` column since round 143, and it reported large
numbers for real titles:

| title | `m3g=` (as printed) |
|---|---|
| 极品飞车之无间风云FX中文版_4518 | 495144 |
| 生化危机3D(CAPCOM经典)_3671 | 54447 |
| 鬼泣3D中文版_3376 | 21551 |
| 仙剑奇侠传(2008)_12406 | 12058 |
| 3D城市桌球_8299 | 9011 |

Those readings drove several rounds of work on the render path.

## What is true

The stats `printf` in `src/runner/nj_run.c` had **twelve conversions and eleven
arguments**: `(unsigned long)s.m3g_renders` was missing, so `m3g=%lu` was fed
`secs`, a `double`, and `time=%.3f` was fed `s.frames`.  Every `m3g=` number
ever printed was the **bit pattern of a double**, not a count.

`-Wall` already warned about it.  The build output is in Chinese and this
project's habit was to grep for `error:` — the compiler said 警告 and the warning
was never read.

With the argument restored (`151d33e`), six real titles that carry JSR 184
content all report `m3g=0`, and a temporary `fprintf` at the top of `NJFN(m3g_render)`
counts zero entries over a 30-frame run.  The conclusion that held before round
143 — "the corpus never reaches the JSR 184 render path" — is the correct one.

## The guard

`CMakeLists.txt` now builds the runner with `-Werror=format`, so a conversion
and its argument disagreeing stops the build.  Teeth: deleting the `m3g_renders`
argument again fails the build with

```
error: 格式 '%lu' expects argument of type 'long unsigned int',
       but argument 8 has type 'double' [-Werror=format=]
```

## Still open

* `Graphics3D.render()` is implemented and draws through `nj_m3g_raster_mesh`,
  but no title in the corpus calls it, so the path is exercised only by
  `tests/test_m3g.c`.  Why the titles stop short of it is not known: the one
  throw they all share is a `NullPointerException` inside their own
  `bc.a(I)Ljavax/microedition/m3g/Node;`, which is their resource lookup, not
  our API.
* The lesson generalises past this column: an instrument that is never
  cross-checked against a second, independent reading is not an instrument.
