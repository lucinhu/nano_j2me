# Ready-to-post PR: libretro-super (core info)

Post this as a pull request against **`libretro/libretro-super`** (branch
`master`, baseline `55e50a81195eaf8dd1b7720a601282459832ced1`, 2026-11-19).
Nothing has been pushed upstream; this file exists so the submission can be
opened without rewriting text.

## Title

`info: add nano_j2me (Mobile - J2ME)`

## Branch / patch

Apply `docs/libretro/upstream/libretro-super-nano_j2me-info.patch` on top of
`55e50a81`:

```
git clone https://github.com/libretro/libretro-super.git
cd libretro-super
git checkout 55e50a81195eaf8dd1b7720a601282459832ced1
git checkout -b add-nano_j2me
git apply /path/to/libretro-super-nano_j2me-info.patch
git add dist/info/nano_j2me_libretro.info
git commit -m "info: add nano_j2me (Mobile - J2ME)"
git push <fork> add-nano_j2me
```

## Body

Adds `dist/info/nano_j2me_libretro.info` for the nano_j2me core, a J2ME (MIDP/CLDC) runtime for RetroArch.

- Upstream source: https://github.com/lucinhu/nano_j2me (release v0.5.20)
- Library page PR: libretro/libretro-docs#1239
- Platform database entry already exists upstream: `libretro-database` `dat/Mobile - J2ME.dat`; the `.info` uses `database = "Mobile - J2ME"`.
- License: MIT for the core, OFL-1.1 for embedded fallback font data, Apache-2.0 for AMR decoder data; the full notices ship with the core. `.info` field: `license = "MIT|OFL-1.1|Apache-2.0"`.
- Builds with the standard libretro CI templates: the repository's root `.gitlab-ci.yml` extends the `libretro-infrastructure/ci-templates` `linux-cmake.yml`, `windows-cmake-mingw.yml` and `android-cmake.yml` with `CORENAME=nano_j2me`, `EXTRA_PATH=libretro`.
- Current version 0.5.20; `savestate = "true"` (`basic`), `core_options` v2, `is_experimental = "true"`.

Validation (offline, on the candidate tree):

- The patch applies cleanly to `libretro-super` master `55e50a81`.
- The `.info` file is byte-identical to the file shipped in the release archive and parses in RetroArch 1.22.2 (core info cache written, no warnings).
- The command shapes from the three upstream CI templates were reproduced locally, producing `linux-<arch>/libretro/nano_j2me_libretro.so`, the Windows `.dll` and the Android `_android.so`.
- A clean clone of the core repository built from the same commit; the resulting core passed the mock-frontend conformance matrix and real RetroArch load/run/exit checks.

## Checklist

- [x] `.info` file added under `dist/info/`
- [x] `corename`, `display_name`, `supported_extensions` (`jar`) set
- [x] `savestate`, `core_options`, `needs_fullpath` set truthfully
- [x] `license` matches the shipped notices
- [x] `database = "Mobile - J2ME"` matches `libretro-database`
- [x] no `.info` keys outside the conventional set
- [ ] CI run of this repository against the PR (maintainer-side)
