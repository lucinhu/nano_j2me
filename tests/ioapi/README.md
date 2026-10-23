# tests/ioapi - Java-level java.io regression fixture

These classes are not run as a MIDlet.  They are compiled with
`javac --release 8`, embedded in `ioapi_fixture.h` as class-file byte arrays and
executed from `tests/test_ioapi.c` through `nj_class_load()` +
`nj_call_static()`.  That is deliberate: the bugs this fixture guards live in
the runtime's constant-pool resolver, and a C-level `nj_call()` names the class
directly, so it never exercises the failing lookup.

What is covered:

* `IoApiProbe` calls `DataInput.readFloat/readDouble` and
  `DataOutput.writeFloat/writeDouble/writeChars` **through interface
  references**, so the interpreter resolves the method on the interface that
  declares it before dispatching on the receiver.  The cases include finite
  values, `-0.0f`, infinities, canonical NaN, truncated input, end of stream,
  and bytes written round-tripped back through `DataInputStream`.
* `LockReader` extends `java.io.Reader`, calls `super()` and `super(lock)` and
  synchronises on the inherited protected `lock` field in overridden
  `read`/`read(char[],int,int)`/`close`/`skip`.  It also checks that
  `Reader(null)` throws `NullPointerException`.
* `RangeOnlyReader` overrides only `read(char[],int,int)`; the inherited
  `read()`, `skip(long)` and `ready()` must dispatch into / follow the CLDC
  defaults (`ready()` is always false for a reader with no stream of its own;
  only `InputStreamReader` probes `available()`).  Before the lock fix `read()`
  and `ready()` treated the lock object as the wrapped `InputStream`.
* `StreamEncoding` tests the real `InputStreamReader` and `OutputStreamWriter`
  constructors and overloads. Its input stream returns only one byte per bulk
  read, so a decoder must survive input fragmentation. Cases cover Latin1
  defaults and aliases, case-insensitive UTF-8/ASCII aliases, normal UTF-8 NUL,
  BMP and supplementary characters, `char[1]` reads with range sentinels,
  `ready()`/`skip()` with a pending low surrogate, writer overloads, and a high
  surrogate held across `flush()` and completed by a later write. Closing a
  dangling high surrogate writes `?`; closed operations throw `IOException`.
  Unknown and empty names throw `UnsupportedEncodingException`; null names
  throw `NullPointerException`. The malformed UTF-8 cases check replacement
  grouping and preservation of the next non-continuation byte independently
  of the writer, so matching encoder/decoder mistakes cannot cancel out.

The expected values are a desktop HotSpot run of the same sources.  To reproduce
the oracle:

```sh
javac --release 8 -d /tmp/ioapi tests/ioapi/*.java
java -Dfile.encoding=ISO-8859-1 -cp /tmp/ioapi ioapi.IoApiDesktop
```

`IoApiDesktop.java` is not embedded; it only prints `tag value` lines and
`tests/test_ioapi.c` carries those values as constants.

After editing a fixture source, regenerate the embedded header:

```sh
javac --release 8 -d /tmp/ioapi tests/ioapi/*.java
python3 tests/ioapi/gen_c_header.py /tmp/ioapi
```

`ioapi_fixture.h` is checked in, so the CMake build needs no Java toolchain and
`tests/test_ioapi.c` stays registered under its existing `nj_add_test` entry.
The fixture uses only desktop `java.io` classes from the JDK, so no MIDP stub
list changes are needed either.

The desktop command fixes `file.encoding` because the runtime's default is
ISO-8859-1 even on a host whose Java default is UTF-8. Individual named-encoding
cases always use explicit names. `NJ_ENABLE_STREAM_UTF8=0` keeps the ten common
Latin1/name/lifecycle cases and replaces UTF-8/ASCII cases with two rejection
cases: every tested UTF-8/ASCII reader and writer constructor must throw
`UnsupportedEncodingException` (result mask 15). These disabled-feature results
are configuration requirements; a desktop JVM supports those encodings.

Sequence digests start at 17 and apply `h = h * 31 + value`, using UTF-16 code
units for reads and unsigned bytes for writes. State tests additionally digest
read counts, range sentinels, `ready()` booleans, skipped counts, or byte lengths
after `flush()`. Exception tests return a bitmask for the individually caught
exception types. This gives each case a deterministic integer without relying
on the runtime's text formatting or default decoding to inspect the answer.

The nested `StreamEncoding$WriterOutput` fixture is embedded and loaded before
`StreamEncoding`. It makes `flush()` fail once and checks that close preserves
the original exception, attempts the wrapped close and rejects later writes,
including empty string writes. Its callback also probes writer reentry while a
surrogate pair is being emitted. `io.enc.local_reentrant_pair` expects 7 for the
local immediate streaming contract; desktop HotSpot returns 1 because it
buffers callbacks differently. That row is explicitly not a desktop oracle
comparison. The C-only writer test additionally guards the small configuration's
legacy low-byte mapping (U+0141/U+0142 → bytes 41/42), whereas the enabled Latin-1
codec replaces unmappable characters with `?`.

Malformed UTF-8 desktop results (`R` means U+FFFD; hex bytes on the left):

| Case | Input bytes | Decoded UTF-16 units |
| --- | --- | --- |
| Invalid lead | `FF 80 41` | `R R 0041` |
| Overlong two-byte | `C0 AF 41` | `R R 0041` |
| Overlong three-byte | `E0 80 AF 41` | `R R R 0041` |
| Overlong four-byte | `F0 80 80 AF 41` | `R R R R 0041` |
| Truncated two-byte | `C2` | `R` |
| Truncated three-byte | `E2 82` | `R` |
| Truncated four-byte | `F0 9F 98` | `R` |
| Bad continuation two-byte | `C2 41 C2 A2` | `R 0041 00A2` |
| Bad continuation three-byte | `E2 82 41 C2 A2` | `R 0041 00A2` |
| Bad continuation four-byte | `F0 9F 98 41 C2 A2` | `R 0041 00A2` |
| Encoded surrogates | `ED A0 80 ED BF BF 41` | `R R 0041` |
| Above U+10FFFF | `F4 90 80 80 41` | `R R R R 0041` |
| Modified-UTF-8 NUL | `C0 80 00 41` | `R R 0000 0041` |

Fail-before evidence was obtained by preserving the current `build/libnj2me.a`
and linking the new formal test source against that saved archive, without
restoring or stashing runtime sources. The original binary passed 190 checks.
The new driver against the same archive passed 194 and failed 31: the four
Latin1 default/alias cases passed, while six name/lifecycle cases and all 25
UTF-8/ASCII cases failed. The desktop output for the new fixture is:

```text
io.enc.default_read 490609510
io.enc.default_write 490609510
io.enc.latin1_alias_read 2062192150
io.enc.latin1_alias_write 2062192150
io.enc.unknown_reader 3
io.enc.unknown_writer 3
io.enc.null_reader 1
io.enc.null_writer 1
io.enc.closed_reader 15
io.enc.closed_writer 31
io.enc.utf8_alias -369754991
io.enc.utf8_read 1319727115
io.enc.split_supplementary 1388573965
io.enc.ready_pending_low 125085354
io.enc.skip_pending_low 1664843219
io.enc.utf8_write 1895888707
io.enc.writer_cross_calls 577745536
io.enc.close_dangling_high 18446
io.enc.closed_empty_string 1
io.enc.failed_close_writer 8191
io.enc.write_bad_surrogates 1188236842
io.enc.ascii_alias -2141736295
io.enc.ascii_read 1312801457
io.enc.ascii_write -1035939219
io.enc.invalid_lead 65515248
io.enc.overlong_two 65515248
io.enc.overlong_three 2033002261
io.enc.overlong_four -1399409776
io.enc.truncated_two 66060
io.enc.truncated_three 66060
io.enc.truncated_four 66060
io.enc.bad_cont_two 63485837
io.enc.bad_cont_three 63485837
io.enc.bad_cont_four 63485837
io.enc.encoded_surrogates 65515248
io.enc.above_limit -1399409776
io.enc.modified_nul 2030970738
```
