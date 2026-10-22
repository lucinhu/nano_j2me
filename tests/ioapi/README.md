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

The expected values are a desktop HotSpot run of the same sources.  To reproduce
the oracle:

```sh
javac --release 8 -d /tmp/ioapi tests/ioapi/*.java
java -cp /tmp/ioapi ioapi.IoApiDesktop
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
