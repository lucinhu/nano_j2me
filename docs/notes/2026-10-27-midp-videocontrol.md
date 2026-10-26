# MIDP `VideoControl`: the corpus titles' movies reach the screen

Date: 2026-10-27
Scope: `src/midp/nj_lib_media.c` (VideoControl natives, direct-video overlay,
video pump gating, supported content types), `src/midp/nj_midp.h`,
`src/midp/nj_lib_lcdui.c` (frame-boundary overlay call),
`src/core/nj_lib_system.c` (`video.snapshot.encodings`), `tests/test_lib.c`
Evidence: `.local-tools/debug-runs/m4v-videocontrol-20261027/`; ctest 118/118.

## The last mile the decoder work left

The MPEG-4 Part 2 decoder, its provider and the Player path were finished in
`a7c597b`..`ccc272f`: the three corpus movies decode pixel-identically to
FFmpeg, inside a Player too.  What no title could do was *show* the result.
`VideoControl` was an interface stub whose `getControl("VideoControl")`
answered null, and both corpus titles that own a movie
(`docs/notes/2026-10-25-doom3-media-audit.md`) dereference the control without a
null check inside their own `catch (Exception)`:

* **1942空战_10403**: `f.a(byte[], int)` creates the `video/3gpp` Player, then
  `f.a(Canvas, ...)` calls `initDisplayMode(1, canvas)`, `setDisplayLocation`,
  `setDisplayFullScreen`, `setDisplaySize`, `setVisible(true)` and
  `Player.start()`.  With a null control the NPE was caught and the intro was
  skipped: the game went straight to its logo and menu.
* **毁灭战士3_10598**: `Doom.video()` does the same on the Canvas `m`, then
  sleeps 12 s while the movie should play.  With a null control it stayed on its
  starfield splash (the audit note predicted exactly this).

## What was implemented

**A real control, not an interface.**  `javax/microedition/media/control/VideoControl` is a concrete class with the `Control` super slot, the two display
mode constants (`USE_DIRECT_VIDEO` = 1, `USE_GUI_PRIMITIVE` = 0), a thin
`cOwner` handle, and natives for the ten methods.  `Player.getControl()` returns
it for a video Player only (`pKind == 2`), and `getControls()` reports
`VolumeControl` + `VideoControl` for such a Player.

**The display state lives on the Player.**  New `pV*` fields record the mode,
the target Canvas, the display location and size, the full-screen flag and the
visibility.  The control methods validate what the spec requires
(`initDisplayMode` needs a realized Player and a Canvas; the display setters
need `initDisplayMode` first; `getSnapshot` needs `PREFETCHED`+, negative sizes
are refused; an unknown snapshot encoding is refused rather than faked).  A
savestate restore clears the Canvas reference and the mode, because a restored
Player must not keep a display object from the saved arena.

**Direct-video overlay at the frame boundary.**  `nj_midp_present()` now calls
`nj_media_video_overlay()` after the Canvas content has been mirrored and before
the port is asked to present.  A started (`STARTED`) and visible video Player
whose control registered this displayable has its RGB565 frame drawn into the
screen surface at the requested rectangle: an image blit when the size equals
the source, nearest-neighbour scaling otherwise, clipped to the display.  The
movie is not part of the Canvas' own `paint()`, which is what
`USE_DIRECT_VIDEO` means - the same place a handset's video overlay sits.

**The pump obeys the state machine.**  `media_video_step()` used to decode one
sample per emulated frame from the moment `createPlayer()` returned, no matter
what state the Player was in; it now advances only while the Player is
`STARTED`, so `stop()` pauses the movie and `start()` resumes from where it was.
`player_start()` also has an explicit video branch: a movie has no audio device
to open, it only has to be on the media registry for the pump to run.

**`getSnapshot("RGB888")`** converts the current RGB565 frame to a byte array
(three bytes per pixel) and `video.snapshot.encodings` now advertises
`RGB888`.  `Manager.getSupportedContentTypes()` also stopped lying: it lists
the six content types this runtime really plays (`audio/x-wav`, `audio/midi`,
`audio/amr`, `audio/amr-wb`, `audio/mpeg`, `video/3gpp`) instead of the single
`audio/x-wav` it had been answering since before MP3/AMR/MIDI/video existed.

## Verification

**API test** (`tests/test_lib.c`, new `test_video_control_mp4v`, driven by the
64x48 mp4v fixture): the control exists and names itself; `initDisplayMode(1,
canvas)` returns null; the display setters round-trip through
`getDisplay*`/`getSource*`; the Player records the mode and visibility;
`getSnapshot("RGB888")` returns a non-black frame of the right size and refuses
an encoding it does not produce; `getControls()` is two controls with one
VideoControl; and a stopped Player does not advance its samples.  `test_lib` is
**912 checks / 0 failures**; the full suite is **118/118**.

**Real titles.**  The same CLI replays as the previous round, on the previous
HEAD (left) and this tree (right), in
`.local-tools/debug-runs/m4v-videocontrol-20261027/`:

| title | before | after |
| --- | --- | --- |
| 1942空战_10403 | splash → 1942 logo → menu, no movie | intro movie drawn over the black movie canvas from frame ~00155 to ~00412, then the menu |
| 毁灭战士3_10598 | starfield splash only | Doom 3 intro clip drawn into the Canvas handed to `initDisplayMode`, then the menu |

The 1942 clip is 176x144 and is scaled to the rectangle the game asked for; the
Doom clip is 320x240 and the game asks for 240x320, so it is stretched to the
requested size exactly as authored.  Both runs are normal exits (exit 0, no
uncaught exception) with `--stats` recorded in their `run.log`.  Side-by-side
sheets: `1942/before-after-sheet.png`, `doom/before-after-sheet.png`.

## Boundaries

* `USE_GUI_PRIMITIVE` (mode 0) is refused with `IllegalArgumentException`: it
  would have to hand back a host GUI component, and this runtime renders video
  only into the MIDlet's own Canvas.
* A movie's own audio track is not played by the video Player (the video track
  is decoded and drawn; the MP4 audio branch is unchanged for separate audio
  Players).  Both corpus movies carry AMR audio, so their intros are silent on
  this runtime; that is a separate `Player`/audio-mixer feature, not a decoder
  gap.
* The display rectangle is taken literally: no rotation, no aspect-preserving
  letterbox, no alpha blending (video is opaque).
* Scaling is nearest-neighbour (whole-image blit when the sizes match).
