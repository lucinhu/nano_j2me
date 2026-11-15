# Ready-to-post PR: libretro/docs (library page)

Post this as a pull request against **`libretro/docs`** (branch `master`,
baseline `177c669990b659e7cfd00add4564b85c2a692179`, 2026-11-16). Nothing has
been pushed upstream; this file exists so the submission can be opened without
rewriting text.

## Title

`docs: add nano_j2me (Mobile - J2ME)`

## Branch / patch

Apply `docs/libretro/upstream/libretro-docs-nano_j2me.patch` on top of
`177c6699`:

```
git clone https://github.com/libretro/libretro-docs.git
cd libretro-docs
git checkout 177c669990b659e7cfd00add4564b85c2a692179
git clean -fdq
git checkout -b add-nano_j2me
git apply /path/to/libretro-docs-nano_j2me.patch
git add docs/library/nano_j2me.md
git add mkdocs.yml docs/guides/core-list.md docs/development/licenses.md docs/meta/see-also.md
git commit -m "docs: add nano_j2me (Mobile - J2ME)"
git push <fork> add-nano_j2me
```

## Body

Adds a library page for the nano_j2me core (a J2ME/MIDP-CLDC runtime for
RetroArch) and lists it next to the existing J2ME cores.

Files changed (5 files, +229 lines):

- `docs/library/nano_j2me.md` — new page with `title`/`description` front
  matter, features, core options, save states, limitations and build notes.
- `mkdocs.yml` — `'Mobile - J2ME (nano_j2me)': 'library/nano_j2me.md'` in the
  `Mobile Emulation` nav group, after FreeJ2ME-Plus.
- `docs/guides/core-list.md` — row for the J2ME row group.
- `docs/development/licenses.md` — MIT / OFL-1.1 / Apache-2.0 split, matching
  the core's shipped notices.
- `docs/meta/see-also.md` — new `## J2ME` section listing FreeJ2ME,
  FreeJ2ME-Plus, nano_j2me and SquirrelJME.

The companion `libretro-super` `.info` PR is described in
`docs/libretro/upstream/PR-libretro-super.md` in the core repository.

Feature statements in the page are the behaviour implemented by the current
core (0.5.8): M3G software 3D, MIDI/MP3/AMR/WAV/ADPCM media, MPEG-4 Part 2 and
H.263 video playback, manual save states, frontend rewind sizing guidance,
cheat search over the exposed JVM arena (`SYSTEM_RAM` memory map). It makes no
determinism/netplay/runahead claim, and states that the exposed memory is a
debugging window, not a save channel.

Validation done before sending this PR (all offline, on the candidate tree):

- Patch applies cleanly to `177c669990b659e7cfd00add4564b85c2a692179` (rebased after the first PR revision).
- `mkdocs build` with the patch applied exits 0 and renders
  `site/library/nano_j2me/index.html`; the only warning is the git-date
  plugin's missing-history note for the new file in a local clone.
- All links in the page are absolute; no relative markdown links.
- The repository's offline bundle validator checks the five-file set, the
  front matter and the patched mkdocs build.

## Checklist

- [x] page added under `docs/library/` with `title`/`description` front matter
- [x] `mkdocs.yml` nav entry in the right group
- [x] `core-list.md` row added
- [x] `licenses.md` row added
- [x] `see-also.md` entry added
- [x] `mkdocs build` passes locally with the patch applied
- [ ] upstream `mkdocs build` CI (maintainer-side)
