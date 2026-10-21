# A DataInputStream that cannot rewind its own stream

`实况足球经理2009_10609` (Gameloft's Real Football Manager 2009) was the last
unexplained failure in the corpus sweep. It booted to its Gameloft logo, then
painted a single flat colour forever, with a `NullPointerException` in its own
`d.T(int)` string cache and an `ArrayIndexOutOfBoundsException` out of
`java/io/InputStream.read([BII)`. The old catalogue entry called it flaky,
because the same binary sometimes executed 208 million bytecodes and sometimes
15 million, but a fresh build reproduces the flat frame every time, and so does
the corpus gate.

## What the title does

Its resource unpacker (`d.c(String)`) opens a compressed container:

1. `Coach.bin` in the JAR is a four-byte big-endian length followed by 22,550
   bytes of a standard `.lzma` ALONE stream.
2. The title reads the length through the resource stream, decodes the LZMA
   payload with its own decoder, and gets 80,275 bytes.
3. That buffer is a simple container: a little-endian count (10), ten
   little-endian offsets, then the entries. Entry 4 starts at offset 1,906 and
   is 69,204 bytes long.
4. It reads the count and the offset table as a `DataInputStream` over a
   `ByteArrayInputStream`, calls `reset()` to return to the start of the
   buffer, `skipBytes(1,906)` to the entry, reads the entry as a slice, and
   parses that slice.

Our LZMA decode of the same bytes is byte-identical to Python's `lzma`
(sha256 `7c62b2b7…`), so the container is fine. The problem was step 4.

## The runtime bug

`src/midp/nj_lib_io.c` had no `mark`/`reset`/`markSupported` declarations on
its built-in `java/io/DataInputStream`, so method resolution fell through to
`java/io/InputStream`'s binding, which is the stream's `close` slot - an empty
operation. `reset()` therefore did nothing. After reading the count and the
offset table the stream sat at offset 44, and `skipBytes(1,906)` moved it to
1,950 instead. The slice the loader read was `decoded[1950:71154]`, not
`decoded[1906:71110]`: the container header - 44 bytes - was still in front of
the entry.

Its parser then read the first entry's length as 2,270 while the "entry" it had
allocated for the string table was 2,079 bytes long, so `read(byte[], off, len)`
threw `ArrayIndexOutOfBoundsException`, the loader thread died, the string
tables `ix`/`iw` stayed null, and every later `paint()` hit `d.T(int)`'s cache
`NullPointerException` and drew nothing but its background.

This was the second half of one loader.  `63b385b` had already corrected
`skipBytes` (it threw `EOFException` on a short stream where CLDC says return
the number of bytes it could skip), and with that fixed the unpacker got past
the length fields and reached the parse that exposed the failed rewind.  The
two fixes together are what the title needs; neither alone is enough.

## The fix

`DataInputStream` is a `FilterInputStream`: mark, reset and markSupported
belong to the stream it wraps. The class table now declares all three, with
natives that fetch the wrapped stream and forward the call:

```c
static nj_s4 dis_mark(nj_vm *vm, nj_ref self, nj_value *args, ...);
static nj_s4 dis_reset(...);
static nj_s4 dis_mark_supported(...);
```

Each one takes `dis_in(self)` and calls the method on the wrapped
`java/io/InputStream` (the underlying stream decides the behaviour, so a
`ByteArrayInputStream` supports marking while a plain stream need not).

That exposed a second, smaller problem: the base `java/io/InputStream` bound
`reset()` to the same empty close slot, so a stream that cannot actually rewind
answered "fine, nothing happened" where CLDC says "throws IOException - if this
stream has not been marked or if the mark has been invalidated". It now throws
`IOException("mark/reset not supported")`, matching the reference. `mark()`
stays a no-op, which is what the specification asks for, and a stream that can
mark overrides both.

## Tests

`tests/test_ioapi.c` gained `test_datainput_mark_reset`, which drives a
`DataInputStream` over a `ByteArrayInputStream("ABCDEFGH")`:

* `readByte()` is `'A'`, `markSupported()` is true (it must forward);
* `reset()` without a mark rewinds to the start, so the next `readByte()` is
  `'A'` again;
* after `'B'`, `mark(0)`, `skipBytes(5)` returns 5 and `reset()` returns to
  `'C'` - the unpacker's exact sequence;
* `skipBytes(100)` near the end reports the 5 bytes it could skip instead of
  running past the buffer;
* a bare `java/io/InputStream` raises `IOException` from `reset()`.

166 assertions pass; binding the DataInputStream `reset` back to the empty slot
fails 5 of them (`readByte() returned 67, want 'B'` and friends), and binding
the base-class `reset` back fails 2, so the tests pin both halves.

## Real-game verification

Fresh RMS, headless 176x208, 16 MiB heap, 30 fps:

* 900 frames: the sound confirmation dialog (`CONFIRMATION / DO YOU WANT
  SOUND?`) appears around frame 300 - the same dialog and the same 9-second
  point the reference emulator shows (`.local-tools/debug-runs/rfmanager-r4/
  fix3/frames/00300.png`).
* 2,100 frames with taps at frames 500, 800, 1100, 1400, 1700 and 2000: the
  main menu appears at frame 900 (`NEW CAREER / SHARE TEAM / ABOUT / OPTIONS /
  EXIT`, `menu1/frames/00900.png`) and the player-creation form at frame 1,900
  (`FIRST NAME / LAST NAME / BIRTHDAY / NATIONALITY / TECH. STYLE`,
  `menu1/frames/01900.png`).
* Statistics of that run: `frames=2100 paints=1033 gfx=292223 time=70.204s
  fps=29.9 bytecodes=914909871 heap=5327872/5391616` - 5.33/5.39 MiB used and
  peak inside the 16 MiB heap.

The title still prints a few exceptions it catches itself: two
`MediaException`s from `Manager.createPlayer` ("empty stream", audio resources
the container ships empty), two `NullPointerException`s in `c.g()`, and one
`d.T(int)` `NullPointerException` on the very first paint before its loader
thread has filled the table. None of them stops the boot; they are the title's
own timing.

The reference emulator is deterministic at this size too (three runs, identical
screenshot hash `3783b5c5`) and reaches the same dialog.  176x208 is the size
both were compared at.  240x320 is not restricted either: the title boots
there past its splash to the dialog
(`.local-tools/debug-runs/rfmanager-r4/size240/`), and the reference reaches the
same splash and keeps running (`.local-tools/debug-runs/rfmanager-r4/ref240/
shot.png`).
