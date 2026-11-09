# RMS enumeration, shared opens and snapshot compatibility

The `RecordEnumeration` implementation in v0.5.5 saved a list of record IDs,
but discarded its filter and comparator. `rebuild()` only reset its cursor and
`keepUpdated()` only changed a boolean. Records added, changed or removed after
creation could therefore leave an allegedly live enumeration stale. Reopening
one record store also created separate Java objects with separate record lists;
closing one of these objects did not implement MIDP's shared-open count.

The normative contract comes from the MIDP 2.0 [RecordEnumeration SDK](https://nikita36078.github.io/J2ME_Docs/docs/midp-2.0/javax/microedition/rms/RecordEnumeration.html)
and [RecordStore SDK](https://nikita36078.github.io/J2ME_Docs/docs/midp-2.0/javax/microedition/rms/RecordStore.html).
FreeJ2ME is useful as an implementation comparison, but differs from the SDK on
several lifetime and repeated-`keepUpdated(true)` cases, so it is not the test
oracle.

## Observable behavior

- New enumerations retain their own filter and comparator. `rebuild()` reapplies
  both to current records. `keepUpdated(true)` rebuilds even when already true.
- Successful add, set and delete operations update subscribed enumerations.
  Disabling updates freezes membership until a rebuild or re-enable. Reading a
  retained ID still retrieves that record's current contents.
- The first backward step after creation or reset returns the last element.
  Traversal stops at either end without wrapping. A failed step does not move
  the cursor. The exact cursor anchor following a rebuild or live update is not
  specified by MIDP; the implementation resets it.
- Availability is false after the store's final close. Reopening the name creates
  a new live store and does not revive the old enumeration. Destroy unregisters
  and releases callback references; later enumeration calls fail with
  `IllegalStateException`.
- Same-name opens reuse the live object, share current data and increment its
  open count. Only the final close closes the store. Deleting an open store
  fails. Repeated opens also avoid reparsing its disk file; no numerical speedup
  is claimed here.

## GC-visible state without changing existing layouts

Two internal classes extend existing Java objects:

- `nj2me/rms/Records` extends `java/util/Vector` and owns the live-enumeration
  registry and shared-open count.
- `nj2me/rms/Enumeration` extends `RecordEnumeration` and owns the original
  filter, comparator and refresh guard.

They add no static fields or methods. Existing `RecordStore`,
`RecordEnumeration` and `Vector` declarations retain their field and method
counts, descriptors and offsets. A legacy records Vector is upgraded lazily by
copying references into a real Vector subclass, then publishing it only after
all copies succeed. Record contents, IDs, next ID and timestamp stay on the
original store object. A failed metadata attachment or shared-open root
allocation leaves the previous Vector, data and open count intact.

The normal GC and snapshot walkers traverse superclass reference fields, so
callbacks, store data and subscriptions survive collection and snapshot reload.
A live subscription retains its enumeration even when no application field does.
Destroy and final close release the registry's references. Refresh copies the
subscription list before callbacks, protects temporary objects with checked
roots, publishes a replacement ID list only on success, and guards against
recursive refresh of the same enumeration.

## Snapshot boundary

Real v0.5.5 snapshots load into the new reader without changing their bytes.
Their old base-class stores can acquire metadata on demand. Old enumerations
cannot recover callbacks that the old writer never saved: these keep their
legacy frozen membership and cursor convention, including on rebuild or
`keepUpdated(true)`. Create a fresh enumeration for the full new behavior.

Snapshots made by this implementation require the new runtime, including
snapshots without RMS objects. Their builtin class table contains the two new
internal classes; v0.5.5 rejects those names before installing the saved heap,
statics or threads. There is no snapshot version/flag workaround that makes the
new classes available in an old reader. The persistent RMS disk format is
unchanged. Keep disk saves and use the matching runtime for fast VM snapshots.

A legacy snapshot can already contain multiple independently modified open
objects with the same store name. Their divergent contents cannot be merged
losslessly. A subsequent open reuses one live instance; it does not rewrite or
merge the others.

## Validation

The same public-API fixture fully runs against both implementations:

| Check | v0.5.5 baseline | This implementation |
| --- | ---: | ---: |
| New enumeration fixture | 618 checks, 93 failures | 776 checks, 0 failures |
| Expanded RMS fixture | 994 passed, 13 failed | 1,005 passed, 0 failed |
| Adjacent library fixture | — | 1,432 checks, 0 failures |

Filter/comparator callbacks assert their receiver identity, so successful live
updates naturally execute more checks. The new enumeration and expanded RMS
fixtures also pass ASan and UBSan. Production and the new fixture compile with
`-Wall -Wextra -Werror`; the existing RMS fixture retains its normal flags due to
preexisting unused declarations.

The 19 root-allocation fault cases cover five genuine cold-open/load paths,
four lazy-metadata/shared-open paths, one already-attached shared-open path,
two record-add roots, six enumeration roots, and a later comparator operand after
a successful comparison. Each injected failure must actually trigger, preserve
the expected outer root count and retain the checked record data. Fresh VMs for
cold loads prevent a deliberately partial parse from overwriting the seed file.

The independent snapshot fixture passes 70 checks in both normal and full
ASan/UBSan/float-cast-overflow builds, including leak detection. Actual f56
writers produce the old inputs: ordinary state loads with 6 checks and an open
base store plus old enumeration loads with 22 checks, both in normal and
sanitized new readers. New ordinary and RMS snapshots are safely rejected by
f56 with 7 checks each, including unchanged target state and a successful fresh
MIDlet start. The existing save and M3G snapshot sanitizer suites pass 2,521 and
95 checks. An 89-line comparison of public fields, methods, instance sizes,
offsets and static counts matches exactly; both processes have 427 static
slots, including the two test-holder fields.

The original Minecraft JAR also reaches the copied world's terrain, held block
and HUD in a 1,600-frame SDL2 replay at a paced 30 fps. The captured window is
byte-identical to the preceding release's capture of that scene. A real f56
Minecraft VM snapshot and a snapshot saved by the new implementation each
resume for 300 frames; their checked 240×320 frame pixels are identical. These
runs use isolated filesystem/RMS copies and preserve the seed and original JAR.
They are compatibility checks, not a speed comparison. The snapshot's preexisting caught
UDP receive exceptions still occur; network-handle restoration is not fixed by
this change.

The default Release build passes the complete **153/153** serial CTest suite
(326.98 seconds in this run). To repeat the registered regression tests:

```sh
cmake -S . -B build -G Ninja
cmake --build build -j4
ctest --test-dir build --output-on-failure -j1
```

The tests do not assign semantics to callback-driven store mutations, callback
invocation order, or transactional rollback when a callback throws during a
mutation. Explicit rebuild exceptions must propagate, balance temporary roots
and permit a later successful rebuild. An unrelated preexisting add failure can
consume a next record ID before insertion; this change does not claim to repair
record-add transaction atomicity, record listeners or the constant version
getter. These remain separate work.
