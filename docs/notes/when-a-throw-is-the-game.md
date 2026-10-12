# Telling a game's own crash from a runtime defect

The corpus reports, for every failure, the first exception and the exact place
it came from:

```
灌篮高手-爆发!樱木_7947  FAIL (nothing was ever presented; first throw:
  java/lang/ArithmeticException at w.paint(Ljavax/microedition/lcdui/Graphics;)V
  pc=91: / by zero)
```

An exception in the game's own method is **not** by itself evidence of a
runtime defect - but neither is it evidence that the game is broken.  The
question that separates the two is: *was this throw supposed to be caught?*

That is answerable without a device.  `javap` prints the method's exception
table, and a throw outside every range in it escapes on any conforming VM:

```
$ unzip -p 灌篮高手-爆发!樱木_7947.jar w.class > /tmp/w.class
$ javap -c -p /tmp/w.class | sed -n '/public void paint(javax/,/^  [a-z]/p' \
    | grep -A2 "Exception table"
      Exception table:
         from    to  target type
           158   183   186   Class java/lang/Exception
```

`paint`'s only handler covers bytes 158..183.  The failing instruction is at
pc 90 - `getWidth()` followed by `iconst_0` and `idiv`, a division by a
**literal zero** - so no handler covers it, and the exception escapes on a real
device too.  Our runtime is right to report it; the title is broken.  (The same
`idiv` pattern is a known obfuscator idiom when it *is* inside a handler, which
is exactly why the table has to be read rather than assumed.)

## Applying it

1. Read the throw site from the corpus line: class, method, pc.
2. `unzip -p <jar> <Class>.class` and `javap -c -p` it.
3. Find the method's `Exception table` and check whether the pc falls in a
   `from..to` range.
4. If it does, and we still escape, the defect is ours - that is a runtime bug
   worth chasing.  If it does not, the title throws on any VM.

Two corpus failures were classified this way in round 151: `灌篮高手-爆发!樱木_7947`
(the division above) and `鲁宾逊漂流记汉化BT版_4677` (`not a MIDlet suite` - no
`MIDlet-1` in the manifest at all).
