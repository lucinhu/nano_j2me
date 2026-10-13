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
  `tests/test_m3g.c`.  **Where the titles stop is now measured**, by putting a
  `nj_logf` at the top of the three entry points and counting the lines:

  | title | `Graphics3D.getInstance()` | `bindTarget()` | `setCamera()` | `clear()` | `Loader.load()` |
  |---|---|---|---|---|---|
  | 极品飞车之无间风云FX中文版_4518 | 1 | 1 | 0 | 0 | 3 |
  | 3D城市桌球_8299 | 1 | 1 | 0 | 0 | 1 |
  | 鬼泣3D中文版_3376 | 0 | 0 | 0 | 0 | 0 |

  So the games do reach the API, get an instance, bind a target, and load a
  container - and then stop before configuring a camera or rendering anything.
  The `Loader.load()` array is not the reason: counting the slots it fills
  gives `nobj=1 nulls=0` for Most Wanted and `nobj=52 nulls=0` for 3D城市桌球,
  so nothing is left null for a game to trip over.  What they hit next is a
  `NullPointerException` in their own code (`bc.a(I)Ljavax/microedition/m3g/Node;
  pc=12`, `c.n()V pc=107`), which is the resource lookup described above.
* The lesson generalises past this column: an instrument that is never
  cross-checked against a second, independent reading is not an instrument.

## Where the scene graph stops now (2026-10-13)

毁灭战士3_10598 was the title that made the scene graph measurable, because it
builds one from `.m3g` files rather than from code.  Four separate defects sat
between `Loader.load()` and a filled `World`, and each one was visible only
because the next one appeared after it was fixed:

* a `World`'s children were never decoded at all - the allocator that keeps the
  object table has no case for type 22, so `find()` walked an empty vector;
* the child list was truncated: `NJ_M3G_MAX_REFS` is 16 and one `World` in
  `/doors/mars.m3g` names 155 children, so `find(889)` answered null and the
  game called `addChild(null)`.  The loader now allocates the overflow;
* the references in a child list can be read three ways - one-based into the
  file's table, zero-based, or matched against each object's `userID` - and
  files disagree, sometimes within themselves (`/mars/mar.m3g` reads as
  `userID`, `/doors/mars.m3g` as one-based, `/rol1/s.m3g` as neither).  The
  loader picks one reading per file, keeping the old per-list choice for a file
  that contradicts itself;
* and when all of that still does not name the object a title asks for,
  `find()` now falls back to the file's own object table, which the loader
  stores on every `World` as `_nj_all`.  A heuristic that misses should cost a
  `find()` its speed, not the title its scene.

Two further gaps were in the member surface rather than the parser: the
transform API (`Group.postRotate`, `translate`, `scale`,
`getCompositeTransform`, `Transform.transpose`, `Node.setScope`,
`Camera.setGeneric`, `Background.setImage`/`setCrop`, ...) did not exist, and
a scene object built with `new` had a **null** matrix, so every transform call
that did exist wrote nothing.  `mat_of()` installs the identity matrix on first
access now; `tests/test_m3g.c` reads each of these back through its getter (130
checks, 0 failures).

With those in place 毁灭战士3_10598 starts and runs - 120 frames, no uncaught
exception, `m3g=2` - and `tris=0` is still the reading, so the open question is
no longer "does the runtime get there" but "which triangle of these meshes
should have been drawn".

## The array `Loader.load()` returns (2026-10-14)

3D拉力赛-进化汉化版_6366 was reported frozen on its way into a race. It reads
the loader's result the way JSR 184 invites - `(World) Loader.load(path)[0]` -
and the loader was handing back `Object3D[nobj + 1]` with slot 0 unused and
every object in the file behind it. Slot 0 was therefore always null, the
`checkcast World` on it threw, the race screen's constructor aborted half
finished and every later `paint()` threw again: 324 NullPointerExceptions in a
700-frame run.

The specification is explicit - "the root level objects in the file; in other
words, those objects that are not referenced by any other objects ... guaranteed
not to contain any null objects" - so the loader now keeps that table (slot
i + 1 holds object i, which is what a reference resolves against and what
`World._nj_all` exposes) and returns a second array of the roots only, a World
first. Which objects are roots needs the same per-file reference reading the
child linking uses, so that reading moved into `m3g_ref_mode_of()` and both
callers share it. A reference from an object to itself, and a reference to a
World, are not marks: two corpus files read their World's trailing pair back as
"camera 0, background = the World's own number", and nothing in the format can
hold a World.

Measured on that title's `/res/arc.m3g` with a probe MIDlet: before,
`len=16 nulls=2 worlds=1 [0]=null`; after, `len=11 nulls=0 worlds=1
firstWorld=0 [0]=World`. Commit `b37580f`.

## Where that title stops now: a Mesh has no submeshes

The next call is `Mesh.getAppearance(0)`, and the loader does not link a Mesh's
submeshes (the comment above `m3g_build_objects` has listed that as outstanding
all along). The symptom is honest and not a freeze:

```
thrown: java/lang/IndexOutOfBoundsException at
    javax/microedition/m3g/Mesh.getAppearance(I)… pc=0: no submeshes
```

Its layout was measured before writing anything, and it did **not** pin down.
Across the corpus, 975 `.m3g` files were loaded by one probe
(`/tmp/m3gscan`, assets extracted from the 18 corpus JARs that ship any), which
decoded 2,906 Meshes. A Mesh looks like this - 4 bytes of userID, the Node
fields, then what reads like `[vertexBuffer][submeshCount]` followed by 8 bytes
per submesh:

| offset | contents | evidence |
|---|---|---|
| 0x00 | userID | u32, matches `find()` |
| 0x0C | enable bits, translation, scale, orientation | floats land in sane ranges |
| 0x35 | three flag bytes | `00 01 01` in the title's four meshes |
| 0x38 | `FFFFFFFF` | constant in every dump |
| 0x3E | a reference | 8, 10, 14, 20 in that title - object numbers |
| 0x42 | submesh count | 1 mostly; 2, 3, 9 seen |
| 0x46 | 8 bytes per submesh | two references: index buffer, appearance |

Of the 2,024 Meshes whose bytes fit in the dump, 1,723 satisfy
`len == 0x46 + 8 * count` exactly. The other 301 do not, and their byte at
0x42 reads as 27, 304, 305 or `0xFFFFFF`, which is a count of nothing - so for
those the layout is shifted, or they carry fields this reading does not know
about, and one rule cannot cover both. Guessing the rule that fits 85% means
loading a submesh list that belongs to another object: a title would draw the
wrong model instead of none, which is the worse failure. What it needs is the
treatment `World` got - one layout accepted only when every field of every
object in the corpus agrees - and then `_nj_subs`, `_nj_ib`, `_nj_vb` and
`_nj_nsub` filled the way `mesh_set_base()` fills them.

