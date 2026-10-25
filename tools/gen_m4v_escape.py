#!/usr/bin/env python3
"""Regenerate include/nj2me/nj_m4v_escape.h from FFmpeg's mpeg4data.h.

The AC escape forms need ff_rl_init()'s two derived tables: for each `last`
half of the RL table, max_level[run] is the largest level at that run and
max_run[level] the largest run at that level.  The rule is copied from
libavcodec/rl.c; the data comes from mpeg4data.h.  Run this only when the
reference tables change.
"""
import io, re, sys

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '.local-tools/debug-runs/m4v-round25/mpeg4data.h'
    src = io.open(path, encoding='utf-8').read()

    def arr(name):
        m = re.search(r'const int8_t\s+' + name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', src, re.S)
        if m is None:
            raise SystemExit('missing ' + name)
        return [int(x) for x in re.findall(r'-?\d+', m.group(1))]

    run_tbl, lvl_tbl = arr('ff_mpeg4_intra_run'), arr('ff_mpeg4_intra_level')
    n, last = 102, 67
    mr_n, ml_n = max(run_tbl) + 1, max(lvl_tbl) + 1
    halves = []
    for half in (0, 1):
        start, end = (0, last) if half == 0 else (last, n)
        mx, mr = [0] * mr_n, [0] * ml_n
        for i in range(start, end):
            r, v = run_tbl[i], lvl_tbl[i]
            if v > mx[r]: mx[r] = v
            if r > mr[v]: mr[v] = r
        halves.append((mx, mr))

    def emit(name, vals):
        body = ''.join('    ' + ', '.join('%3d' % v for v in vals[i:i + 12]) + ',\n'
                       for i in range(0, len(vals), 12))
        return 'static const nj_u1 %s[%d] = {\n%s};\n' % (name, len(vals), body)

    out = '/* Generated from FFmpeg mpeg4data.h by tools/gen_m4v_escape.py.  Do not edit. */\n'
    out += '#ifndef NJ_M4V_ESCAPE_H\n#define NJ_M4V_ESCAPE_H\n\n'
    out += '#define NJ_M4V_MAX_RUN   %d\n#define NJ_M4V_MAX_LEVEL %d\n\n' % (mr_n - 1, ml_n - 1)
    for half in (0, 1):
        out += emit('nj_m4v_max_level_%d' % half, halves[half][0]) + '\n'
        out += emit('nj_m4v_max_run_%d' % half, halves[half][1]) + '\n'
    out += '#endif\n'
    io.open('include/nj2me/nj_m4v_escape.h', 'w', encoding='utf-8').write(out)
    print('wrote include/nj2me/nj_m4v_escape.h')

if __name__ == '__main__':
    main()
