# Android storage: a full disk, read-only data and a kill mid-save (2026-11-03)

Date: 2026-11-03.  Device: emulator-5560 (AVD OpenHV_API26_googleapis, x86_64,
Android 8.1); APK built from this branch with `tools/build_android_apk.sh --abi
x86_64`; title `tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar`.  Evidence:
`.local-tools/debug-runs/r7-storage/` (screenshots, logcat streams, the APK,
`appdata-baseline.tar` 22,032,896 B).  The world used is the one the game
creates itself ("Unnamed world", Creative), saved before each scenario so the
filesystem state is known.

## What was tested and how

Every scenario starts from the same recipe: force-stop, restore the baseline
`/data/data/com.lucinhu.nj2me/files` from `appdata-baseline.tar`, fix
ownership/labels (see the last section), then drive the game with `input tap` /
`input keyevent`.  Failures are injected with three tools:

- disk full: append bytes to the world's `chunks.dat`, then fill `/data` with
  `dd` files until `df` reports a few blocks;
- read-only data: `chmod 555` on the world directory, or `chmod 400` on
  `chunks.dat`;
- process death: `kill -9` at a chosen point (polling the temporary file size,
  or a fixed delay after the save key press).

The world's files before any scenario (baseline): `chunks.dat` 22,024,192 B
(md5 `bde3d4a273fa55109a5eb2e7cfd97037`), `entities.dat` 52 B
(`bacad7ec65da9ac5da54c26966047f70`), `level.dat` 787 B
(`bbeb77d04d61d5b271f0c0623a55d7f5`).  A normal creation run reports
`MCJ: GEN 25906 ms, save 5532 ms`; a normal quit-to-title reports
`MCJ: offset write ok`, `MCJ: chunks in place 9`, `MCJ: SAVE 191 ms`.

The world list and the build dialog are drawn inside the game canvas, so taps
are placed with the canvas-to-framebuffer mapping measured on this AVD:
`fb = canvas + (40,32)` at 320x640 (Start Game at fb (140,162), world card at
fb (160,215), Create new at fb (240,84)).  That mapping was derived by
registering a desktop headless title frame (`d1-desktop-title.png`) onto the
emulator screenshot at scale 1, offset (40,32).

## The matrix

| # | Scenario | What happened |
|---|----------|---------------|
| 1 | `adb install -r` over an existing install | Success; world files unchanged (same md5s); world loads (`MCJ: LOAD read 7505 ms, prepare 7989 ms`). Evidence `logcat-upgrade.log`, `u1-worldlist.png`, `u2-loaded.png`. |
| 2 | `chunks.dat` grown by 2 bytes, `/data` full (Available 2,611 blocks), quit-to-title | Game's `Storage.saveAll` threw `java.io.IOException: cannot write the file` from `java/io/FileOutputStream.write([BII)V`; game logged `MCJ ERROR: save: java.io.IOException: cannot write the file` and `MCJ: SAVE 927 ms`. Leftover `chunks.tmp` 10,694,656 B; `chunks.dat` unchanged for its first 22,024,192 bytes (baseline hash), only the 2 appended bytes remained; `level.dat` updated. App stayed alive on the title screen; after deleting the fill files the next save succeeded and the temporary file was renamed into place. Evidence `logcat-enospc.log`, `e1-after-save-attempt.png`, `e2-after-enospc.png`, `r1-worldlist-after-enospc.png`, `r2-inworld-after-enospc.png`. |
| 3 | `/data` full (Available 7 blocks), normal in-place quit save | Succeeded: `MCJ: offset write ok`, `MCJ: chunks in place 30`, `MCJ: SAVE 593 ms`. In-place overwrites need no new allocation. Evidence `logcat-fullsave.log`, `e3-inplace-enospc.png`. |
| 4 | world directory `chmod 555`, quit-to-title | `java.io.IOException: cannot create` at `javax/microedition/io/file/FileConnection.create()V`; game logged `MCJ ERROR: save: ...`, `SAVE 1 ms`; no original file touched. Evidence `logcat-readonly-dir.log`, `ro1-after-save-dir.png`. |
| 5 | `chunks.dat` `chmod 400`, in-place save | `FileOutputStream.<init>` threw `java.io.IOException: cannot open the file`; the game logged `MCJ ERROR: chunks in place: ...` and fell back to a full save, which succeeded (`SAVE 2719 ms`), replacing the read-only file (tmp + rename, final mode 0600). Evidence `logcat-readonly-file.log`, `ro2-after-save-file.png`. |
| 6 | kill -9 1.2 s into a full save (after growing `chunks.dat`) | `chunks.tmp` 12,165,124 B partial; `chunks.dat` old (22,024,194 B) intact; `level.dat` updated; `entities.dat` unchanged. Relaunch lists the world, loads it (`LOAD read 6532 ms, prepare 8008 ms`), the next save succeeds (`SAVE 2405 ms`). Evidence `logcat-kill-save.log`, `k1`..`k4`. |
| 7 | kill -9 during the initial world-creation save (tmp 3,649,540 B) | Leftover `Unnamed world-` with `level.dat` + `chunks.tmp` 3.6 MB; the older world untouched. Evidence `logcat-kill-create.log`. |
| 8 | kill -9 120 ms after quit in the in-place save window | No file changed at all (all three md5s still the baseline) — the kill landed before the save began writing. No torn file. Evidence `logcat-kill-inplace.log`. |
| 9 | world creation with `/data` nearly full (Available 5,189 blocks, ~20 MB) | Save threw `java.io.IOException: cannot write the file` from `FileOutputStream.write`, `MCJ ERROR: save: ...`; the game continued (`GEN 26572 ms, save 4835 ms`) and loaded the attempted world empty. Leftover directory `Unnamed world-` with only `level.dat` + `chunks.tmp` 21,241,856 B (no `chunks.dat`/`entities.dat`), which the world list then showed next to the good world (`p1-worldlist-partial.png`). Loading it gives an empty world (`LOAD read 2 ms, prepare 139 ms, world blocks 0K`), no crash. Evidence `logcat-create-enospc.log`, `c3-after-create-enospc.png`, `c4-inworld-after-failed-create.png`, `b1-only-broken-list.png`, `b2-load-broken.png`, `logcat-brokenworld.log`. |
| 10 | app start with `/data` full (Available 3 blocks) | Launches and reaches the title normally; no app exception (the system itself logs `SQLiteFullException` for its own processes). After `pm clear` frees space, world creation works. Evidence `logcat-fullstart.log`, `f1`..`f4`. |
| 11 | uninstall + fresh install, and a signature-mismatch APK | Uninstall removes `files/`; fresh install starts clean and works (new `firstInstallTime`/`lastUpdateTime`). An APK re-signed with a different key fails `adb install -r` cleanly with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`; the installed app and its data are untouched. Evidence `logcat-restored*.log`, `device-fs-restored/`, `g1`..`g5`. |
| 12 | restoring app data with `tar` after a reinstall | Restored files got the new uid (`u0_a95`/10095) but wrong ownership/SELinux labels, so the runtime threw the expected `RecordStoreException: cannot open the record store file for writing` on the options store and `java.io.IOException: cannot create directory` from `FileConnection.mkdir()` in the game's `Storage.init()`; the app still reached the title. With `chown -R 10095:10095` + `restorecon -R`, the world card reappeared and the world loaded/saved normally (`LOAD read 6968 ms, prepare 8337 ms`, `SAVE 125 ms`). This is the test harness, not the runtime: Android gives app data the MLS categories `s0:c512,c768`, and a plain `tar` restore leaves `s0`. Evidence `logcat-final-smoke2.log`, `final2-list.png`, `final2-inworld.png`, `final2-gamemenu.png`, `final2-quit-title.png`. |

## What the runtime does with the errors (source map)

Everything the game met is the JSR-75 / JSR-120 exception type it should be,
with the message pointing at the failing operation:

| Operation | Failure | Where |
|-----------|---------|-------|
| `FileConnection.create()` on an existing file / a read-only directory | `IOException "already exists"` / `"cannot create"` | `src/midp/nj_lib_fc.c` (`fc_create`) |
| `FileConnection.mkdir()` | `IOException "cannot create directory"` | `src/midp/nj_lib_fc.c:1046` |
| `FileConnection.truncate(n)` | negative `n` -> `IllegalArgumentException "negative file length"`; read-only connection -> `IllegalModeException`; missing file/directory -> `IOException "cannot truncate the file"` | `src/midp/nj_lib_fc.c:760` |
| `FileConnection.rename()` when the target exists | `IOException "rename target is not available"` (the game deletes the target first, which is why its full save works) | `src/midp/nj_lib_fc.c:1102` |
| `FileOutputStream` constructor | `IOException "cannot open the file"` | `src/midp/nj_lib_io.c:1787` (`fos_init`) |
| `FileOutputStream.write` | `IOException "cannot write the file"`, stream position unchanged | `src/midp/nj_lib_io.c` (`fos_write_data`) |
| `RecordStore` open for writing | `RecordStoreException "cannot open the record store file for writing"` | `src/midp/nj_lib_rms.c` (`rms_open`) |
| short read | clamped to the file size, `-1` at EOF | `src/midp/nj_lib_io.c` (`fis_read_host`) |

No scenario produced a crash, an ANR, a silently truncated save outside the
game's own save design, or a file left in a state the next launch could not
recover from.  The leftovers (`chunks.tmp`, the listed-but-empty
`Unnamed world-`) are produced by the game's save logic after its own
`IOException`; the runtime reports the error and keeps running, which is its
contract.  They are game-side findings (like the LAN `ClassNotFoundException`
recorded in `docs/COMPATIBILITY.md`) and are not hidden here by rewriting the
runtime.

## Two deviations a product decision should close

Both predate this round; neither was reproduced through Minecraft, so nothing
was changed silently.

1. **`FileConnection.truncate(n)` with `n` greater than the file size is a
   silent no-op.**  `src/midp/nj_lib_fc.c:760` documents and implements "JSR 75
   never extends a file": it only calls `ftruncate()` when the new length is
   smaller, and returns normally otherwise.  JSR 75's `truncate` javadoc
   defines `IllegalArgumentException` for a length larger than the file
   (as well as for a negative one).  `tests/test_fs.c` and
   `docs/COMPATIBILITY.md` currently assert the no-op as intended behaviour, so
   changing it is a compatibility decision, not a bug fix.
2. **`FileConnection.totalSize()`, `usedSize()` and `directorySize()` are
   absent.**  The built-in class table (`src/midp/nj_lib_fc.c:1250-1276`) and
   the implementations expose `fileSize()` and `availableSize()` only; a
   MIDlet that calls any of the three gets `NoSuchMethodError`.  They are part
   of the JSR-75 `FileConnection` interface (the rest of the interface is
   complete: `README`-level list above).  Implementing them needs a volume
   stat (`statvfs`) and, for `directorySize()`, a bounded recursive walk.

`fc_rename()` refusing an existing target is stricter than POSIX replace but is
what JSR-75 specifies, and the title works around it; it is deliberately left
as is.

## Gates

- Full worktree test suite: **137/137 passed** (`ctest --test-dir build
  --output-on-failure`, total 251.73 s, exit 0); desktop build `ninja: no work
  to do` after a clean 248/248 link.
- Final on-device smoke after the tests: fresh launch → world list → load
  (`LOAD read 6968 ms, prepare 8337 ms, left 183/744, heap 8947K/90112K`) →
  in-world → game menu → Quit to title (`offset write ok`, `chunks in place 6`,
  `SAVE 125 ms`) → title screen.  Evidence `logcat-final-smoke2.log`,
  `final2-*.png`.
- The APK used is `apk/nj2me.apk` (919,705 B, x86_64) built from this branch and
  installed over the previous release APK after copying the canonical
  `build/android/debug.keystore` into the worktree.

## Conclusion

No runtime product bug was found in the storage-failure paths: every injected
failure (ENOSPC, read-only directory, read-only file, process kill at several
points, upgrade, reinstall, signature mismatch) produced a JSR-conformant
exception, no crash and no unrecoverable state.  The two genuine gaps are the
long-standing `truncate` extension deviation and the three missing FileConnection
size methods; both should be decided explicitly (fix or document as
unsupported), which is why this round is report-only rather than a runtime
patch.  The SELinux/chown hiccup belongs to the test harness: any restore of
app data outside PackageManager has to fix ownership and MLS categories, and
that is now recorded here so the next round does not lose an hour to it.
