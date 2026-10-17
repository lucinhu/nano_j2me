# Interface method resolution: Object methods and inherited declarations

Status (2026-10-16): inherited interface method resolution fixed and covered by
`tests/test_resolution.c`. The Object-method symptom below was not reproducible
in the current runtime: `getClass`, `hashCode`, `equals` and `toString` already
resolve through a parsed interface's `java/lang/Object` super link. The original
2026-10-13 investigation is preserved below as historical context.

## Verified fix and regression

A Java fixture declares `Root.value()I`, `Left extends Root`, `Right extends
Root`, `Leaf extends Left, Right`, and a concrete `Impl implements Leaf`.
`javac --release 8 -g:none` emits `invokeinterface resolve/Leaf.value:()I` even
though Leaf has no declaration of its own. The embedded test lowers the class
version to 48; it uses no Java 8 language features and needs no JDK at test time.

Before the fix, both the ordinary call and the null-receiver case failed with:

```
java.lang.NoSuchMethodError: resolve/Leaf.value()I @ resolve.Probe.throughLeaf+5
```

`method_lookup_named()` now falls back to the interface graph after searching
the class/superclass declarations. It follows parsed interface lists and the
super links used by built-in interfaces. A visited bitmap avoids repeated
visits in diamonds and cycles. Inherited static/private methods and methods
with a different descriptor cannot satisfy the reference. Existing class-chain
precedence and the constant-pool/receiver caches remain in use.

The regression executes two inherited calls (result 84), Object-method calls
(including overridden `hashCode` and `toString`), and a null receiver caught as
`NullPointerException`. Fresh VMs also verify that inherited static/private
methods and a mismatched descriptor fail symbolic resolution. No HTTP union
method tables were removed. Java 8 default-method conflict resolution is outside
this CLDC fix.

Validation: `cmake --build build -j4` succeeded and CTest passed 80/80 tests
(147.22 seconds). A paced 240×320 headless replay of 《3D版田园物语》 ran
480 frames at 30 fps with isolated RMS data, reached the title menu and advanced
opening story text, then exited with status 0. Its log still contains five
`DataInputStream.readUTF` null-input-stream exceptions; this replay is a startup
and input regression, not proof of complete game compatibility. Logs and sampled
frames are under `.local-tools/debug-runs/interface-resolution-20261016/`.

## Historical report (2026-10-13)

Found while implementing `javax.microedition.io.HttpConnection`. The following
symptom, cause and proposed work describe that earlier investigation.

## Symptom

A MIDlet that holds an interface-typed reference and calls an `Object` method
on it dies:

```
thrown: java/lang/NoSuchMethodError at NjcDemo.run()V pc=…:
    javax/microedition/io/HttpConnection.getClass()Ljava/lang/Class;
```

The receiver is a perfectly good object and `getClass()` exists on
`java/lang/Object`. The same call through a class-typed reference works.

## Cause

`invokevirtual` resolution starts at the class named in the constant pool and
walks *up the superclass chain*. When the named class is an **interface**,
there is no superclass chain to walk: the runtime looks the method up in the
interface's own table and stops. `java/lang/Object`'s methods are therefore
unreachable through an interface type, even though the JVM specification
requires exactly those methods (`getClass`, `hashCode`, `equals`, `toString`)
to resolve.

`src/midp/nj_lib_http.c` works around the sibling problem — an interface's
*inherited interface* methods — by declaring the union of `Connection`,
`InputConnection`, `OutputConnection`, `ContentConnection` and
`HttpConnection` on `HttpConnection` itself. Without that,
`c.openOutputStream()` and `c.getType()` both failed with `NoSuchMethodError`
on a reference whose implementation class answers them. That workaround says
the same thing from the other side: this runtime does not walk interface
hierarchies.

## What a fix looks like

Two pieces, in `src/core/nj_class.c`:

1. method lookup for `invokevirtual`/`invokeinterface` on an interface type
   should fall back to `java/lang/Object` for the five methods every object
   has (`getClass`, `hashCode`, `equals`, `toString`, `clone` is protected and
   belongs nowhere);
2. interface inheritance should be walked for method lookup, which needs an
   interface list on class entries — built-in classes are registered with a
   single super, and `src/core/nj_class.c` already carries
   `ninterfaces`/`interfaces` for parsed classes, so the built-in path is what
   is missing.

Neither is hard; both want a test that covers a MIDlet interface-typed
reference and the built-in interfaces this library already has
(`Player`/`Controllable` was the first place the problem showed up — that one
is fixed, see `tests/test_midi.c`).

## Why it has not mattered yet

A MIDlet normally reaches an `Object` method through a class-typed reference
(`Object o = …; o.getClass()`), and the corpus in `tests/games` does not call
`getClass()` on an interface at all. `HttpConnection` and `Player` references
are the two shapes that make it reachable, and both now declare the methods a
MIDlet actually calls on them.
