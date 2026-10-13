# OPEN: an interface-typed reference cannot reach java.lang.Object

Status: unfixed. Found while implementing
`javax.microedition.io.HttpConnection` (2026-10-13). Nothing in the runtime
depends on it working, and the fix touches method resolution, which is the one
place a mistake breaks every title at once — so it is written down rather than
patched in a hurry.

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
