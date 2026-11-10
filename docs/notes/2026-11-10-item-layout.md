# MIDP Item layout flags and JBenchmark 2

The unmodified JBenchmark 2.1.1 constructs its UI subtest with layout 515
(`LAYOUT_CENTER | LAYOUT_NEWLINE_AFTER`). The runtime rejected every bit above
`0xff`; its Gauge constructor therefore threw `IllegalArgumentException`,
leaving a white Canvas instead of completing the benchmark's UI subtest.

Accept the documented alignment and MIDP 2.0 directive bits (`0x7f33`) in Item
and ImageItem setters and both ImageItem constructors. Keep unknown bits
invalid, start Items at `LAYOUT_DEFAULT`, and expose ImageItem's constructor
layout through inherited `Item.getLayout()`. Correct the swapped EXPAND and
VSHRINK static constant values to 0x800 and 0x1000 respectively.

The source of the flag values and combination rules is the
[Oracle MIDP 2.0 Item API](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/Item.html).
This change does not claim to implement every Form layout behavior.

Validation: the ASan/UBSan LCDUI suite passes three complete VM lifecycles,
including layout 515 on Gauge and ImageItem, constructor/getter consistency,
the complete legal mask, invalid flags, and the corrected constants.
