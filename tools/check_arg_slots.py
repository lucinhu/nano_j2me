#!/usr/bin/env python3
"""Find nj_value argument arrays that are smaller than the descriptor needs.

vm_invoke copies m->arg_slots nj_value entries out of the caller's array, and a
long or double takes TWO slots.  A one-entry array passed to a "(J)V" callee
therefore makes the callee read eight bytes past the array - which is how
Long.valueOf(String, int) was reported by AddressSanitizer.  The read usually
"works" because the callee only looks at the first entry, so this has to be
found by looking at the source, not by running it.
"""
import re
import sys
from pathlib import Path

CALL = re.compile(r'\b(nj_call|nj_call_special|nj_call_static)\s*\(')
DECL = re.compile(r'\bnj_value\s+(\w+)\s*\[\s*(\d+)\s*\]')
DESC = re.compile(r'"\(([^"]*)\)')


def slots(desc):
    """Slots the argument list of a descriptor such as "(J)V" needs.

    The closing ")" is not an argument: stopping at it matters, because
    counting it as one slot made every descriptor look one slot wider than it
    is and produced false positives on correct arrays.
    """
    n = 0
    i = 1
    while i < len(desc) and desc[i] != ')':
        c = desc[i]
        if c in 'JD':
            n += 2
        elif c == 'L':
            n += 1
            while i < len(desc) and desc[i] != ';':
                i += 1
        elif c == '[':
            while i < len(desc) and desc[i] == '[':
                i += 1
            if i < len(desc) and desc[i] == 'L':
                while i < len(desc) and desc[i] != ';':
                    i += 1
            n += 1
        else:
            n += 1
        i += 1
    return n


def calls(text):
    """Yield (start, body) for each call, body being the balanced argument list."""
    for m in CALL.finditer(text):
        i = m.end()
        depth = 1
        start = i
        while i < len(text) and depth:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        yield m.start(), text[start:i - 1]


def main():
    root = Path(__file__).resolve().parent.parent
    bad = 0
    for path in sorted((root / 'src').rglob('*.c')):
        text = path.read_text(encoding='utf-8', errors='replace')
        decls = {m.group(1): int(m.group(2)) for m in DECL.finditer(text)}
        if not decls:
            continue
        for _, body in calls(text):
            names = [n for n in decls if re.search(r'\b' + n + r'\b', body)]
            if not names:
                continue
            d = DESC.search(body)
            if not d:
                continue
            need = slots('(' + d.group(1) + ')')
            for n in names:
                if decls[n] < need:
                    line = text[:text.index(body)].count('\n') + 1
                    print(f'{path.relative_to(root)}: nj_value {n}[{decls[n]}] '
                          f'is passed to something needing {need} slot(s)')
                    bad += 1
    print(f'{bad} argument array(s) too small')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
