# Preserve completed GameCanvas frames during slow rendering

Robinson's Requiem (`鲁宾逊漂流记汉化版_702.jar`) draws its map in a worker
using `GameCanvas.getGraphics()` and calls `flushGraphics()` after the draw.
On a slow target, several scheduler frames may pass while the next buffer is
incomplete. Ordinary desktop execution does not readily expose that interval.

Two runtime paths copied this incomplete buffer to the display: the adaptive
paint fallback after an application missed its previous cadence, and the
implicit repaint queued after every key or pointer event. Pressing a movement
key could therefore expose the unfinished map even when LCD buffer ownership
and scaling were correct.

An explicit valid GameCanvas flush now marks buffer ownership in bit 1 of the
existing per-VM `app_painted` state; synchronous paints retain bit 0. Quiet frames
and input delivery leave the last committed screen in place. Key callbacks,
held-key state and pointer callbacks still run, and an explicit repaint queued
by the application is neither removed nor ignored. `serviceRepaints()` retains
flush ownership. Existing display transitions reset the cadence/ownership.
Ordinary Canvas and non-flushing legacy GameCanvas fallback painting remain.
No new object field, retained image, mutable global or per-frame allocation is
introduced.

`test_gamecanvas_flush` drives the public Display/Graphics API and observes
presented pixels. It pauses a partially drawn frame for 310 pump cycles, sends
key and pointer edges, crosses idle-counter saturation, verifies explicit
repaint/service delivery, switches displayables and checks the legacy fallback.
Three VM lifecycles pass 9,651 checks under ASan/UBSan. The original quiet-buffer
regression failed six checks; after fixing that path, the input-specific
regression still failed 21 checks, proving that the input repaint needed its
own correction. Eight affected/updated tests pass together. The Java flush
fixture additionally presents 120 frames with zero mixed red/blue frames.

ESP32-S31 firmware, device sampling and the original JAR identity are recorded
in the Korvo parent repository. The original JAR is unchanged and is not part
of this commit.
