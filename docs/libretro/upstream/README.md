# Upstream submission bundle (prepared, not submitted)

These files are ready to copy into the two upstream repositories when the
project authorizes an actual pull request. Nothing here has been uploaded or
opened as a PR; the core repository itself is pushed to
`git@github.com:lucinhu/nano_j2me.git`, but libretro upstream interaction
remains an explicit, manual decision.

Prepared 2026-11-15. The exact-index patches below were generated against and
verified to apply cleanly to `libretro/docs` master
`d83b34b21b8a2c14db573aea66d876e343d316ca` and `libretro-super` master
`fb15e630655375a50317be2a7953477ef9a5d12a`.

## 1. `libretro/libretro-super`

Apply the prepared patch (adds exactly one file), or copy it by hand:

```
git apply libretro-super-nano_j2me-info.patch
# equivalent to:
nano_j2me_libretro.info  ->  dist/info/nano_j2me_libretro.info
```

The copy in this directory is byte-identical to
`src/port/libretro/nano_j2me_libretro.info`
(SHA-256 `c0f30aeae22150208034202986b3db77fd2f423874e2f2bcc1ce7b07c82723c6`).
Suggested PR title: `Add nano_j2me core information for J2ME MIDlets`.
A draft body is in `docs/LIBRETRO_SUBMISSION.md` ("Draft PR descriptions").

## 2. `libretro/libretro/docs`

Apply the prepared patch, which adds the page and every index update in one
diff (5 files, +211 lines):

```
git apply libretro-docs-nano_j2me.patch
```

By hand, copy the page and update the four indexes (all paths relative to the
docs repository root):

```
nano_j2me.md  ->  docs/library/nano_j2me.md
```

- `mkdocs.yml`: add one nav entry in the library section, alphabetically with
  the other `Mobile - ...` pages:
  `- Mobile - J2ME (nano_j2me): library/nano_j2me.md`
- `docs/guides/core-list.md`: add a row for
  `Mobile - J2ME (nano_j2me)` like the neighbouring emulator entries.
- `docs/development/licenses.md`: add a `nano_j2me` entry describing
  `MIT` runtime code, `Apache-2.0` built-in AMR numeric data and
  `OFL-1.1` bundled font data (see the `.info` `license` field).
- `docs/meta/see-also.md`: the patch adds a `## J2ME` section listing
  FreeJ2ME, FreeJ2ME-Plus, nano_j2me and SquirrelJME as related cores.
- BIOS, softpatching, RetroAchievements pages: not applicable (core declares
  none of these).

Validate in the docs checkout:

```sh
python3 -m pip install -r docs/requirements.txt
mkdocs build
```

Suggested PR title: `Document the nano_j2me J2ME core`. A draft body is in
`docs/LIBRETRO_SUBMISSION.md`.

## 3. Core repository

The root `.gitlab-ci.yml` is already committed here and extends the upstream
CI templates. Registration of the repository/mirror with the libretro buildbot,
pipeline execution and artifact collection are maintainer actions; provide the
repository URL, core identifier `nano_j2me`, artifact subdirectory `libretro`,
desired jobs and the validation record when contacting them.

## What remains manual

- Opening the two pull requests and answering upstream review comments.
- Waiting for a libretro maintainer to register/rerun CI.
- Optional retroarch-assets icons and real libretro-database entries for
  playlist support.
- Acceptance, platform selection and release timing are upstream decisions.
