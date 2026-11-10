# Form viewport and text layout

Forms retain a per-instance pixel scroll offset and reveal the focused item.
Items taller than the viewport scroll by half a page before traversal leaves
the item. Moving back up, returning to a Form, programmatic focus and deleting
content reuse the same layout. Pointer hit-testing includes the scroll offset.

Text measurement and painting share word wrapping and LF/CRLF handling.
Preferred item heights and image dimensions participate in layout; labels and
CustomItem placement use the measured height. Form content is clipped to its
viewport so scrolling cannot paint over the title/ticker or soft-key bar.
Noninteractive progress Gauges no longer consume traversal keys or change value.

`test_form_scroll` checks real overflow pixels, wrapped oversized content,
scrolled pointer focus, unchanged screen chrome, end-of-content clamping and
progress Gauge navigation over three create/destroy cycles with identical host
release counts. The regression fails on `6a13581`; it and `test_lcdui` pass with
ASan/UBSan after the fix. Hardware UI/benchmark records live in the Korvo parent.
