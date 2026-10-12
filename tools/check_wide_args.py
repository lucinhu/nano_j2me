#!/usr/bin/env python3
"""Find natives that read a long or double argument from the wrong slot.

`args` in a native is a pointer to the RAW OPERAND SLOTS, not to one entry per
Java value: a long or a double occupies two slots, with the payload in the
lower one.  So in

    Math.max(long, long)          slots: [a_lo a_hi b_lo b_hi]

the second argument lives at index 2, and reading it as NJ_ARG_LONG(args, 1)
splices a's high half onto b's low half.  The result is never "a slightly wrong
number": Math.max(0L, 60L) came out as 0, so a frame pacer written as

    Thread.sleep(Math.max(0 - elapsed, 60L))

asked for a sleep of 4,400 years and the game never drew another frame.  The
caller's own code looked perfect, and every C-level unit test agreed with the
wrong index because nj_call() packs one value per slot.

This gate reads the native tables and the NJFN bodies and checks the index.
"""
import re
import sys
from pathlib import Path

# { "class", "name", "(desc)ret", fn_name }
BINDING = re.compile(
    r'\{\s*"(?P<cls>[^"]+)"\s*,\s*"(?P<name>[^"]+)"\s*,\s*'
    r'"(?P<desc>\([^"]*\)[^"]*)"\s*,\s*(?P<fn>\w+)\s*\}')
NJFN = re.compile(r'NJFN\((?P<fn>\w+)\)\s*\{')
ARG_WIDE = re.compile(r'NJ_ARG_(?:LONG|DOUBLE)\(\s*args\s*,\s*(\d+)\s*\)')


def slot_offsets(desc):
    """Yield (param_index, slot_offset, is_wide) for each parameter."""
    i = 1
    idx = 0
    off = 0
    while i < len(desc) and desc[i] != ')':
        c = desc[i]
        if c == '[':
            while i < len(desc) and desc[i] == '[':
                i += 1
            if i < len(desc) and desc[i] == 'L':
                while i < len(desc) and desc[i] != ';':
                    i += 1
            yield idx, off, False
            off += 1
        elif c == 'L':
            while i < len(desc) and desc[i] != ';':
                i += 1
            yield idx, off, False
            off += 1
        elif c in 'JD':
            yield idx, off, True
            off += 2
        else:
            yield idx, off, False
            off += 1
        idx += 1
        i += 1


def body_of(text, fn):
    m = re.search(r'NJFN\(' + re.escape(fn) + r'\)\s*\{', text)
    if not m:
        return None
    i = m.end()
    depth = 1
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[m.end():i]


def main():
    root = Path(__file__).resolve().parent.parent
    bad = 0
    checked = 0
    for path in sorted((root / 'src').rglob('*.c')):
        text = path.read_text(encoding='utf-8', errors='replace')
        for b in BINDING.finditer(text):
            fn = b.group('fn')
            body = body_of(text, fn)
            if body is None:
                continue
            wide = [(i, off) for i, off, w in slot_offsets(b.group('desc')) if w]
            if not wide:
                continue
            checked += 1
            used = {int(n) for n in ARG_WIDE.findall(body)}
            for _, off in wide:
                if off == 0:
                    continue
                if off not in used:
                    line = text[:b.start()].count('\n') + 1
                    print(f'{path.relative_to(root)}:{line}: {b.group("cls")}.'
                          f'{b.group("name")}{b.group("desc")} - the wide argument '
                          f'at slot {off} is never read as NJ_ARG_*(args, {off})'
                          f' (found indices {sorted(used) or "none"})')
                    bad += 1
    print(f'{checked} wide-argument native(s) checked, {bad} wrong')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
