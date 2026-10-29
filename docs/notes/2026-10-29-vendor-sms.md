# Samsung's SM/SMS, and what the fast corpus gate was really measuring

Two things landed together because one led to the other: a vendor API three
titles need, and the reason the parallel crash sweep reported five failures
where there are none.

## com.samsung.util.SM/SMS

The classes did not exist.  Three collected titles build a billing or share
message with the exact same four calls:

    三国塔防－曹操传_1610, 宠物王国2紫_5550, 魂斗罗3代-完全版_3784:
        SM sm = new SM();
        sm.setData(String);
        sm.setDestAddress(String);
        SMS.send(sm);

With the vendor classes absent, that feature died on `NoClassDefFoundError`
inside each title's own code.  A device without an SMS transport is a normal
device, so the honest implementation is the Samsung API's own contract:

| call | answer |
| --- | --- |
| `new SM()` | a value object |
| `setData(String)` / `getData()` | keeps what was set |
| `setDestAddress(String)` / `getDestAddress()` | keeps what was set |
| `SMS.isSupported()` | `false` - this runtime has no SMS transport |
| `SMS.send(SM)` | `IOException`, which the Samsung API declares - not a fabricated success |

`src/midp/nj_lib_samsung.c` holds the two classes and their natives;
`tools/midp_stubs.py` declares them so a fixture can compile against the same
API.  `tools/check_samsung_sms.sh` (ctest `check_samsung_sms`) runs the four
calls through the interpreter and asserts the line above; on the build before
this change it reports
`SMS ERROR java.lang.NoClassDefFoundError: com/samsung/util/SMS`.

## The sweep's shared save directory

`tools/corpus_fatal.sh` runs every title with one shared `--rms-dir`.  Most
titles name their record store "config", so under 8-way parallelism one title
opened another game's records and could fail on data that game never wrote.
That is what the sweep reported for `3D台球之王_5014` (`rc=1`) - the same
command run alone, or eight copies of it in parallel with their own directory,
exits 0.

The script now gives every title its own save directory.  The sweep went from
5/400 to 4/400, and `3D台球之王_5014` disappeared from the failure list.

## The four that remain, and why none is a runtime defect

The gate needs a baseline or it can never be green: three titles fail for their
own reasons and one JAR is not a zip.  `tools/corpus_fatal_known.txt` lists them
with a reason each, and `corpus_fatal.sh` reports them as `known` instead of
`fatal`.  A title NOT on the list is always fatal, so the list is a baseline,
not a place to hide a regression.

| title | verdict |
| --- | --- |
| 阿尔卑斯牧场物语_7043 | its splash canvas is built by the constructor that never creates the `Sound` object; `showNotify` calls `CallCheck` on it and the title's own NPE escapes.  The title still draws and animates (101 colours over 200 frames) |
| 斩妖伏魔录_6231 | the JAR ships `s60/01.png`, the title loads `/s60/02.png`; the failed init leaves a field null and its `showNotify` NPE escapes.  Still draws |
| 1945空战英雄_6105 | its own platform gate: it refuses to run unless `microedition.platform` starts with "Nokia".  Runs with `--device nokia6230` |
| 鲁宾逊漂流记汉化BT版_4677 | the JAR is not a zip at all (`End-of-central-directory signature not found`): a truncated download, not a runtime failure |

Result on the current build:

    fatal 0 / 400, known 4  (heap 16, 150 frames, fps 30)

The wall clock is ~4.5 minutes on 8 cores, which is cheap enough to run after
any change to the event, paint or storage paths.

## Follow-up: the escaped exception now says where it was

Triage of the two "escaped" titles above started by disassembling the class
named in the one-line report by hand.  The frame loop now prints the threads'
remaining frames under that line, in the shape of a Java stack trace, with a
header that says what it is:

    exception escaped into the frame loop: java.lang.NullPointerException @ c.showNotify+6
        frames at the frame boundary (the throwing thread may already have unwound):
        at   c.run()V pc=58

It is best effort: the frames the exception unwound through are gone by the
time the frame loop sees it, so what is printed is what the other threads were
doing at that boundary.  That is still what turns "NPE at c.showNotify+6, no
context" into "NPE while thread 2 sits in c.run()" without a debugger, and the
usual log throttle bounds a title that throws every frame.
