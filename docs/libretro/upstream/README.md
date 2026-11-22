# Upstream submission bundle (PRs opened 2026-11-16)

The project author authorized libretro upstream submission on 2026-11-16, and
the prepared bundles were opened as pull requests from forks:

* `libretro/libretro-super#2204` — `info: add nano_j2me (Mobile - J2ME)`
  (branch `add-nano_j2me` in `lucinhu/libretro-super`, commit `d26aab1`; rebuilt for 0.5.29 on 2026-11-22)
* `libretro/docs#1239` — `docs: add nano_j2me (Mobile - J2ME)`
  (branch `add-nano_j2me` in `lucinhu/docs`, commit `3316299`)

The files in this directory remain the exact copies used for those PRs, plus
the validation notes and the manual checklist. If upstream requests changes,
update the fork branch and regenerate the patch from it.

Prepared 2026-11-15. The exact-index patches below were generated against and
verified to apply cleanly to `libretro/docs` master
`96ee874` (refreshed 2026-11-22 because libretro/docs moved; the previous baseline was `9399ee32829094ab6f2ea51add4bb1562312412e`) and `libretro-super` master
`55e50a81195eaf8dd1b7720a601282459832ced1` (rebased 2026-11-19 and reused for the 0.5.20/0.5.21 metadata commits on 2026-11-21; the patch also applied to the earlier `fb15e63`).

Re-check the whole bundle offline (including clean-clone apply and a docs
site build when you point it at scratch clones):

```sh
tools/check_libretro_submission_bundle.sh \
  --super-clone /path/to/libretro-super --docs-clone /path/to/libretro-docs \
  --apply-clones --mkdocs /path/to/mkdocs
```

It also runs without clones and is registered as the
`check_libretro_submission_bundle` CTest case.

## 1. `libretro/libretro-super`

Apply the prepared patch (adds exactly one file), or copy it by hand:

```
git apply libretro-super-nano_j2me-info.patch
# equivalent to:
nano_j2me_libretro.info  ->  dist/info/nano_j2me_libretro.info
```

The copy in this directory is byte-identical to
`src/port/libretro/nano_j2me_libretro.info`
(SHA-256 `88b6276d42f6dc8af2c8f17139c1aa86efad45437337e757d833eb8c9c95e81c`, version 0.5.29).
Suggested PR title: `Add nano_j2me core information for J2ME MIDlets`.
The ready-to-post title, patch commands, body text and checklist are in
[`PR-libretro-super.md`](PR-libretro-super.md).

## 2. `libretro/libretro/docs`

Apply the prepared patch, which adds the page and every index update in one
diff (5 files, +261 lines):

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
  `'Mobile - J2ME (nano_j2me)': 'library/nano_j2me.md'`
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

Suggested PR title: `Add nano_j2me (Mobile - J2ME)`. The ready-to-post
title, commands, body text and checklist are in
[`PR-libretro-docs.md`](PR-libretro-docs.md).

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
