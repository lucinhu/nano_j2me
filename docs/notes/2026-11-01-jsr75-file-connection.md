# JSR 75 FileConnection: a closed connection deleted the file

The file connection implementation was written as a thin wrapper over the
`--fs-dir` sandbox: paths were mapped, streams opened, and the rest answered
from whatever the Unix file system said.  That is close enough for a game that
saves a file and stops, and wrong in the places JSR 75 spends its rules on.
Two of the answers were actively destructive.

## What was wrong

* **A closed connection still worked.**  Closing only released the file
  descriptor; every method kept running.  `delete()` after `close()` really
  unlinked the file, `create()` after `close()` really created it.  JSR 75
  says every method on a closed connection throws
  `ConnectionClosedException`.
* **Modes were ignored.**  A `Connector.open(path, Connector.READ)`
  connection could call `create()`, `mkdir()` and `delete()`; a `WRITE`-only
  connection could call `exists()`, `fileSize()`, `lastModified()`, `list()`,
  `canRead()`/`canWrite()`/`isHidden()` and `openInputStream()`.  JSR 75 gives
  each of those calls a `SecurityException`/`IOException`/`IllegalModeException`
  answer - refusing is the specified behaviour, not a courtesy.
* **Metadata was invented.**  `fileSize()` of a missing file answered 0
  instead of -1, a directory's size was a number instead of the
  `IOException` the spec asks for, `lastModified()` of a missing file
  answered the current time, `getName()` of a directory lost its trailing
  slash, and `availableSize()` answered the *file's own length* where the
  volume's free space is required.

## What changed (`0dc3b4a`)

`src/midp/nj_lib_fc.c` grew `fc_require_read()` / `fc_require_write()`: they
check the closed flag first (throwing `ConnectionClosedException`) and then
the mode recorded in `F_MODE` (bit 1 read, bit 2 write), so the destructive
methods cannot run on a read connection and the query methods cannot run on a
write-only one.  `fc_getName()` now stats the target and appends `/` when it
is a directory (the sandbox root stays `""`); `fc_fileSize()` answers -1 when
the stat fails and throws `IOException` for a directory; `fc_lastModified()`
answers 0 for a missing file; `fc_availableSize()` uses `statvfs()`
(`f_bavail * f_frsize`) instead of the file length; and
`canRead()`/`canWrite()`/`isHidden()` share one `access()`-based helper.

## Deliberate divergences (reported, not fixed)

* `getPath()` returns the path *including the file name*; an existing test
  pins that string and the connector is mounted at a synthetic root.
* `list(null)` raises the same way the default filter would (a mismatch with
  the reference, pinned by an existing test).
* Dotfiles are listed.
* `setFileConnection()` is not implemented; it is not part of the surface the
  collected titles use.

## One API not added: `isOpen()`

The obvious completeness fix - an `isOpen()` method - breaks saved states.
The saved-state loader compares each builtin class's instance/field/method/
array counts with the class the binary carries (`src/core/nj_save.c:1767`);
a mismatch is a full refusal (`NJ_ERR_FORMAT`).  Adding one method to
`FileConnection` changed its saved method count from 28 to 29 and every
`world.state` created by an older binary refused to resume:

```
class javax/microedition/io/file/FileConnection: saved inst=80 f=5 m=28 arr=0
have inst=80 f=5 m=29
nj_vm_load: refused (NJ_ERR_FORMAT) at src/core/nj_save.c:1767
```

The method was removed again and the rule is recorded in the commit: builtin
classes are part of the saved ABI, so API completeness cannot be bought by
adding methods to them without invalidating states.  (A saved state also
needs `--heap 32` if it was written by a 32 MB-heap run - a 34 MB blob does
not fit the default 16 MB `NJ_SAVE_BLOB_MAX`.  That is unrelated to this fix,
but it is the second reason a resume can look silently refused.)

## Verification

`tests/test_fs.c` went from 66 checks / 24 failures against the old behaviour
to **132 checks / 0 failures**; `tools/make_fs_jar.sh` regenerates the
`tests/fixtures/fsdemo.jar` that the Java half needs (fixture jars are
gitignored build products).  A Minecraft desktop resume from the saved state
loads and plays; worktree ctest is 137/137 (the main tree 140/140).  The
Android "storage init: cannot create directory" report that started this work
turned out to be a test artifact: a `pm clear` followed by an `adb push` of
the world as root leaves a root-owned tree, so the app's uid cannot even stat
`files/fs`; after `chown -R <uid>:<uid> files/fs` (and, on API 26+,
`restorecon -R files/fs` for the SELinux MLS label) the world lists and loads
normally.  Evidence: `.local-tools/debug-runs/mc-bugs-r2-storage-artifact/`.
