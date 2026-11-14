#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Rebuild H.263 numeric VLC data from ITU-T H.263 (01/2005).

The canonical input contains coding facts from Tables 2, 7, 8, 12, 13, 14, 16
and Figure 14.
Use --extract PDF to re-extract it from the hash-pinned official publication.
No other decoder implementation is an input. See docs/M4V_TABLE_PROVENANCE.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / 'tools/data/h263-vlc-2005.json'
PDF_SHA256 = '9dc7e2528fa13ac9d3b6669996b053f6cea7897b5e1ecdd46a253e858ffd2c45'
PDF_URL = 'https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-H.263-200501-I!!PDF-E&type=items'
NOTICE = 'Numeric codewords independently extracted from ITU-T H.263 (01/2005).'


def extract(pdf):
    """Read the published bitstrings, retaining source table row identifiers."""
    if hashlib.sha256(pdf.read_bytes()).hexdigest() != PDF_SHA256:
        raise ValueError('H.263 PDF SHA256 differs from pinned January 2005 publication')
    text = subprocess.check_output(['pdftotext', '-layout', str(pdf), '-'], text=True)

    def section(begin, end):
        return text.split(begin, 1)[1].split(end, 1)[0]

    def code(bits, count):
        bits = ''.join(bits.split())
        if not re.fullmatch('[01]+', bits) or len(bits) != int(count):
            raise ValueError('invalid published bitstring: ' + bits)
        return bits

    def mcbpc(table, end):
        rows = []
        src = section('Table %d/H.263 – VLC table for MCBPC' % table, end)
        pattern = r'^\s*(\d+)\s+(\d+|Stuffing)\s+([01]{2}|–)\s+(\d+)\s+([01 ]+)\s*$'
        for m in re.finditer(pattern, src, re.M):
            index, mode, cbpc, length, bits = m.groups()
            rows.append([int(index), mode if mode == 'Stuffing' else int(mode),
                         None if cbpc == '–' else int(cbpc, 2), code(bits, length)])
        assert [r[0] for r in rows] == list(range(9 if table == 7 else 25))
        return rows

    formats = []
    src = section('Table 2/H.263 – Number of pixels per line', 'For all picture formats,')
    for m in re.finditer(r'^\s*(sub-QCIF|QCIF|CIF|4CIF|16CIF)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$', src, re.M):
        name, width, height, chroma_width, chroma_height = m.groups()
        assert int(width) == int(chroma_width) * 2 and int(height) == int(chroma_height) * 2
        formats.append([name, int(width), int(height)])
    assert [row[0] for row in formats] == ['sub-QCIF', 'QCIF', 'CIF', '4CIF', '16CIF']

    quant = []
    src = section('Table 13/H.263 – DQUANT codes', '5.3.7   Motion Vector')
    for m in re.finditer(r'^\s*(\d+)\s+([–\-]?\d+)\s+([01]{2})\s*$', src, re.M):
        index, delta, bits = m.groups()
        quant.append([int(index), int(delta.replace('–', '-')), bits])
    assert [row[0] for row in quant] == list(range(4))
    assert all(row[0] == int(row[2], 2) for row in quant)

    src = section('6.2.3   Zigzag positioning', 'Figure 14/H.263 – Zigzag')
    scan = [[int(v) for v in m.group().split()]
            for m in re.finditer(r'^\s*\d+(?:\s+\d+){7}\s*$', src, re.M)]
    assert len(scan) == 8 and sorted(v for row in scan for v in row) == list(range(1, 65))

    cbpy = []
    src = section('Table 12/H.263 – VLC table for CBPY', '5.3.6   Quantizer')
    for m in re.finditer(r'^\s*(\d+)\s+[01]{2}\s+[01]{2}\s+(\d+)\s+([01 ]+)\s*$', src, re.M):
        index, length, bits = m.groups()
        cbpy.append([int(index), code(bits, length)])
    assert [r[0] for r in cbpy] == list(range(16))

    motion = []
    src = section('Table 14/H.263 – VLC table for MVD', '5.3.8   Motion Vector')
    pattern = r'^\s*(\d+)\s+([–\-\d.]+)\s+(?:[–\-\d.]+\s+)?(\d+)\s+([01 ]+)\s*$'
    for m in re.finditer(pattern, src, re.M):
        index, vector, length, bits = m.groups()
        # Store half-sample integer displacement, not implementation table index.
        displacement = int(float(vector.replace('–', '-')) * 2)
        motion.append([int(index), displacement, code(bits, length)])
    assert [r[0] for r in motion] == list(range(64))

    tcoef = []
    src = section('Table 16/H.263 – VLC table for TCOEF', 'Table 17/H.263 – FLC table')
    pattern = r'^\s*(\d+)\s+([01])\s+(\d+)\s+(\d+)\s+(\d+)\s+([01 ]+)s\s*$'
    for m in re.finditer(pattern, src, re.M):
        index, last, run, level, length, bits = m.groups()
        # The published bit count includes the following sign bit, excluded here.
        tcoef.append([int(index), int(last), int(run), int(level), code(bits, int(length)-1)])
    assert [r[0] for r in tcoef] == list(range(102))
    escape = re.search(r'^\s*102\s+ESCAPE\s+(\d+)\s+([01 ]+)\s*$', src, re.M)
    assert escape
    return {
        'source': {'standard': 'ITU-T H.263 (01/2005)', 'url': PDF_URL, 'sha256': PDF_SHA256},
        'columns': {'table2': ['format','luma_width','luma_height'],
                    'table13': ['row','quantizer_delta','bits'],
                    'figure14': 'One-based scan order at each raster position',
                    'table7': ['row','macroblock_type','cbpc','bits'],
                    'table8': ['row','macroblock_type','cbpc','bits'],
                    'table12': ['row','bits'], 'table14': ['row','half_sample_displacement','bits'],
                    'table16': ['row','last','run','level_magnitude','bits_without_sign']},
        'table2': formats, 'table13': quant, 'figure14': scan,
        'table7': mcbpc(7, 'Table 8/H.263 – VLC table'),
        'table8': mcbpc(8, 'An extra codeword'), 'table12': cbpy,
        'table14': motion, 'table16': tcoef,
        'escape': code(escape[2], escape[1]),
    }


def canonical(data):
    """Compact one factual row per line for review against the cited tables."""
    result = ['{', '  "source": ' + json.dumps(data['source']) + ',',
              '  "columns": ' + json.dumps(data['columns']) + ',']
    for name in ('table2', 'table7', 'table8', 'table12', 'table13', 'table14', 'table16', 'figure14'):
        result.append('  "' + name + '": [')
        result.append(',\n'.join('    ' + json.dumps(row) for row in data[name]))
        result.append('  ],')
    result += ['  "escape": ' + json.dumps(data['escape']), '}', '']
    return '\n'.join(result)


def vlc_array(ctype, name, entries):
    lines = ['static const %s %s[%d] = {' % (ctype, name, len(entries))]
    lines += ['    { %4d, %2d, %3d },' % tuple(e) for e in entries]
    return '\n'.join(lines + ['};'])


def scalar_array(ctype, name, values, hex_values=False):
    lines = ['static const %s %s[%d] = {' % (ctype, name, len(values))]
    for first in range(0, len(values), 12):
        row = values[first:first+12]
        lines.append('    ' + ', '.join(('0x%04x' % x if hex_values else str(x)) for x in row) + ',')
    return '\n'.join(lines + ['};'])


def maxima(events, field, fixed, length):
    # A maximum is determined by the set of represented events at that coordinate.
    # Empty sets map to zero, the sentinel expected by the decoder's escape path.
    return [[max((e[field] for e in events if e[1] == last and e[fixed] == coordinate), default=0)
             for coordinate in range(length)] for last in (0, 1)]


def matrix_array(name, matrix):
    lines = ['static const nj_u1 %s[2][64] = {' % name]
    for row in matrix:
        lines.append('    {')
        lines += ['        ' + ', '.join(map(str, row[i:i+16])) + ',' for i in range(0, 64, 16)]
        lines.append('    },')
    return '\n'.join(lines + ['};'])


def validate_prefixes(entries):
    strings = [format(code, '0%db' % length) for code, length, _ in entries if length]
    assert len(strings) == len(set(strings))
    assert not any(a != b and b.startswith(a) for a in strings for b in strings)


def replace_array(text, name, replacement):
    pattern = r'static const [^;\n]+\b' + re.escape(name) + r'(?:\[[^\]]+\])+\s*=\s*\{.*?\};'
    text, count = re.subn(pattern, lambda _: replacement, text, count=1, flags=re.S)
    assert count == 1, 'missing output array ' + name
    return text


def generate(data):
    def table7_symbol(row):
        _, mode, cbpc, bits = row
        return [int(bits, 2), len(bits), 8 if mode == 'Stuffing' else cbpc + (4 if mode == 4 else 0)]

    def table8_symbol(row):
        _, mode, cbpc, bits = row
        # Nano's decoder uses flag bits: intra=4, quant delta=8, four-vector=16.
        flags = {0: 0, 1: 8, 2: 16, 3: 4, 4: 12, 5: 24}
        return [int(bits, 2), len(bits), 20 if mode == 'Stuffing' else flags[mode] + cbpc]

    intra = [table7_symbol(row) for row in data['table7']]
    inter = sorted([table8_symbol(row) for row in data['table8']], key=lambda e: e[2])
    inter_slots = {e[2]: e for e in inter}
    padded_inter = [inter_slots.get(index, [0, 0, index]) for index in range(28)]
    cbpy = [[int(bits, 2), len(bits), index] for index, bits in data['table12']]
    motion = [[1, 1, 0]]
    by_displacement = {displacement: bits for _, displacement, bits in data['table14']}
    for magnitude in range(1, 33):
        # For nonzero MVD the final bit selects sign; consume it separately.
        # The one wrapped value (-32) is valid with either sign at half-range.
        negative = by_displacement[-magnitude]
        if magnitude < 32:
            positive = by_displacement[magnitude]
            assert positive[:-1] == negative[:-1] and positive[-1] == '0' and negative[-1] == '1'
        prefix = negative[:-1]
        motion.append([int(prefix, 2), len(prefix), magnitude])
    events = data['table16']
    ac = [[int(e[4], 2), len(e[4]), e[0]] for e in events]
    escape = [int(data['escape'], 2), len(data['escape']), 102]
    for entries in (intra, inter, cbpy, motion, ac + [escape]):
        validate_prefixes(entries)
    assert sum(1 for e in events if e[1] == 0) == 58
    assert all(e[1] == (e[0] >= 58) for e in events)

    intra_path = ROOT / 'src/midp/nj_vlc_intra.h'
    text = intra_path.read_text()
    end = text.index('#ifndef NJ_VLC_INTRA_H')
    text = ('/* H.263/MPEG-4 macroblock and motion codeword data.\n'
            ' * ' + NOTICE + '\n'
            ' * Rebuild: python3 tools/gen_h263_tables.py\n'
            ' * Source/derivation: docs/M4V_TABLE_PROVENANCE.md.\n'
            ' * Lookup helpers below are preserved project code. */\n') + text[end:]
    text = re.sub(r'/\* Motion vector VLC,.*?\*/', '/* Table 14: unsigned magnitude prefix; sign is read separately. */', text, flags=re.S)
    text = re.sub(r'/\* inter MCBPC:.*?\*/', '/* Table 8, mapped to decoder flags; zero-length slots are unused. */', text, flags=re.S)
    for name, entries in [('nj_vlc_intra_mcbpc', intra), ('nj_vlc_cbpy', cbpy),
                          ('nj_vlc_mvtab', motion), ('nj_vlc_inter_mcbpc', padded_inter)]:
        text = replace_array(text, name, vlc_array('nj_vlc_entry', name, entries))
    outputs = {intra_path: text}

    # Enumerate every lookahead. An entry describes the unique codeword prefix
    # of the lookahead; escape and unassigned prefixes retain the invalid sentinel.
    lookup = []
    for lookahead in range(4096):
        matches = [(width << 8) | symbol for bits, width, symbol in ac
                   if lookahead >> (12 - width) == bits]
        assert len(matches) <= 1
        lookup.append(matches[0] if matches else 0xffff)
    for lookahead in range(4096):
        bits = format(lookahead, '012b')
        expected = [e for e in events if bits.startswith(e[4])]
        assert lookup[lookahead] == ((len(expected[0][4]) << 8) | expected[0][0] if expected else 0xffff)
    chunks = ['/* ' + NOTICE + '\n * Generated by tools/gen_h263_tables.py; do not edit arrays.\n'
              ' * See docs/M4V_TABLE_PROVENANCE.md for the pinned source and derivation. */',
              '#ifndef NJ_VLC_INTER_H\n#define NJ_VLC_INTER_H\n',
              '/* Table 16 has 102 events, of which the first 58 have LAST=0. */',
              vlc_array('nj_vlc_entry', 'nj_vlc_inter_ac', ac),
              scalar_array('nj_s1', 'nj_vlc_inter_level', [e[3] for e in events]),
              scalar_array('nj_s1', 'nj_vlc_inter_run', [e[2] for e in events]),
              matrix_array('nj_vlc_inter_max_level', maxima(events, 3, 2, 64)),
              matrix_array('nj_vlc_inter_max_run', maxima(events, 2, 3, 64)),
              scalar_array('nj_u2', 'nj_vlc_inter_ac_lut', lookup, True), '#endif\n']
    # Keep the established lookahead array's name (used by the MPEG-4 decoder).
    old = (ROOT / 'src/midp/nj_vlc_inter.h').read_text()
    names = re.findall(r'static const nj_u2 (\w+)\[4096\]', old)
    assert len(names) == 1
    chunks[-2] = scalar_array('nj_u2', names[0], lookup, True)
    outputs[ROOT / 'src/midp/nj_vlc_inter.h'] = '\n\n'.join(chunks)

    h263_path = ROOT / 'src/midp/nj_h263.c'
    text = h263_path.read_text()
    for name, column in [('h263_format_width', 1), ('h263_format_height', 2)]:
        text = replace_array(text, name, scalar_array('nj_u2', name, [0] + [row[column] for row in data['table2']]))
    raster_scan = [v for row in data['figure14'] for v in row]
    zigzag = [raster_scan.index(coefficient) for coefficient in range(1, 65)]
    text = replace_array(text, 'h263_zigzag', scalar_array('nj_u1', 'h263_zigzag', zigzag))
    text = replace_array(text, 'qtab', 'static const int qtab[4] = { ' +
                         ', '.join(str(row[1]) for row in data['table13']) + ' };')
    for name, entries in [('h263_intra_mcbpc_vlc', intra), ('h263_inter_mcbpc_vlc', sorted(inter, key=lambda e: (e[1],e[0]))),
                          ('h263_cbpy_vlc', sorted(cbpy, key=lambda e: (e[1],e[0]))),
                          ('h263_mv_vlc', sorted(motion, key=lambda e: (e[1],e[0]))),
                          ('h263_tcoef_vlc', sorted(ac + [escape], key=lambda e: (e[1],e[0])))]:
        text = replace_array(text, name, vlc_array('h263_vlc', name, entries))
    text = replace_array(text, 'h263_tcoef_rl', scalar_array('nj_u2', 'h263_tcoef_rl', [(e[2] << 8) | e[3] for e in events]))
    note = '/* ' + NOTICE + '\n * Arrays below: tools/gen_h263_tables.py; docs/M4V_TABLE_PROVENANCE.md. */\n'
    if note not in text:
        text = text.replace('static const h263_vlc h263_intra_mcbpc_vlc', note + 'static const h263_vlc h263_intra_mcbpc_vlc', 1)
    outputs[h263_path] = text
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', type=Path, help='Re-extract canonical numeric facts from the pinned official PDF')
    parser.add_argument('--check', action='store_true', help='Verify generated outputs without writing')
    args = parser.parse_args()
    if args.extract:
        data = extract(args.extract)
        content = canonical(data)
        if args.check:
            if DATA.read_text() != content:
                raise SystemExit('canonical numeric data differs from the pinned PDF')
        else:
            DATA.parent.mkdir(parents=True, exist_ok=True)
            DATA.write_text(content)
    else:
        data = json.loads(DATA.read_text())
    assert data['source']['sha256'] == PDF_SHA256
    changed = []
    for path, text in generate(data).items():
        if path.read_text() != text:
            changed.append(str(path.relative_to(ROOT)))
            if not args.check:
                path.write_text(text)
    if args.check and changed:
        raise SystemExit('regeneration required: ' + ', '.join(changed))
    print('PASS: pinned H.263 numeric data, prefix uniqueness, all 4096 lookaheads; ' +
          ('generated files current' if args.check else 'regenerated ' + str(len(changed)) + ' files'))


if __name__ == '__main__':
    main()
