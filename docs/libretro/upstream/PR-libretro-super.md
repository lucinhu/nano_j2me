# Ready-to-post PR: libretro-super (core info)

Post this as a pull request against **`libretro/libretro-super`** (branch
`master`, baseline `fb15e630655375a50317be2a7953477ef9a5d12a`, 2026-11-14).
Nothing has been pushed upstream; this file exists so the submission can be
opened without rewriting text.

## Title

`info: add nano_j2me (Mobile - J2ME)`

## Branch / patch

Apply `docs/libretro/upstream/libretro-super-nano_j2me-info.patch` on top of
`fb15e63`:

```
git clone https://github.com/libretro/libretro-super.git
cd libretro-super
git checkout fb15e630655375a50317be2a7953477ef9a5d12a
git checkout -b add-nano_j2me
git apply /path/to/libretro-super-nano_j2me-info.patch
git add dist/info/nano_j2me_libretro.info
git commit -m "info: add nano_j2me (Mobile - J2ME)"
git push <fork> add-nano_j2me
```

## Body

Adds `dist/info/nano_j2me_libretro.info` for the nano_j2me core, a J2ME
(MIDP/CLDC) runtime for RetroArch.

- Upstream source: https://github.com/lucinhu/nano_j2me
- Library page (separate PR against `libretro/libretro-docs`): see
  `docs/libretro/upstream/PR-libretro-docs.md` in that repository.
- Platform database entry already exists upstream: `libretro-database`
  `dat/Mobile - J2ME.dat`, and the `.info` uses
  `database = "Mobile - J2ME"`.
- License: MIT for the core, OFL-1.1 for embedded fallback font data,
  Apache-2.0 for AMR decoder data; full notices are shipped in the core
  archive and named in `NOTICE`/`docs/licenses/`. The `.info` field is
  `license = "MIT|OFL-1.1|Apache-2.0"`.
- The core builds with the standard libretro CI templates: the repository
  carries the usual root `.gitlab-ci.yml` extending
  `libretro-infrastructure/ci-templates` `linux-cmake.yml`,
  `windows-cmake-mingw.yml` and `android-cmake.yml` with
  `CORENAME=nano_j2me`, `EXTRA_PATH=libretro`.
- Current version 0.5.13; `savestate = "true"` (`basic`), `core_options` v2,
  `is_experimental = "true"`.

Validation done before sending this PR (all offline, on the candidate tree):

- `libretro-super` patch applies cleanly to `fb15e630655375a50317be2a7953477ef9a5d12a`.
- The `.info` file is byte-identical to the file shipped in the release
  archive and parsed by RetroArch 1.22.2 (`Core info` cache written, no
  parse warnings).
- The three upstream CI template command shapes were reproduced locally and
  all produced an artifact at the expected path
  (`build/linux-<arch>/libretro/nano_j2me_libretro.so`, the Windows `.dll`
  and the Android `_android.so`).
- A clean `git clone` of the core repository built from the same commit and
  the resulting core passed the mock-frontend conformance matrix and real
  RetroArch load/run/exit checks.

## Checklist

- [x] `.info` file added under `dist/info/`
- [x] `corename`, `display_name`, `supported_extensions` (`jar`) set
- [x] `savestate`, `core_options`, `needs_fullpath` set truthfully
- [x] `license` matches the shipped notices
- [x] `database = "Mobile - J2ME"` matches `libretro-database`
- [x] no `.info` keys outside the conventional set
- [ ] CI run of this repository against the PR (maintainer-side)
