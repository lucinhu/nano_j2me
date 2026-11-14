#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Regenerate MPEG-4 intra/DC tables from pinned MIT OxideAV numeric input.

--extract accepts a local checkout of the exact upstream revision. Every
consumed file, including LICENSE, must match its pinned SHA-256. Normal checks
use the canonical JSON and never download or execute upstream code.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / 'tools/data/m4v-oxideav-tables.json'
OUTPUT = ROOT / 'include/nj2me/nj_m4v_tables.h'
REVISION = 'ebfef14e48acf63547f73a9a0f4c36a153db92b2'
UPSTREAM = 'https://github.com/OxideAV/oxideav-mpeg4video'
HASHES = {
    'LICENSE': '4ecae6b086e0d2663c8629e7d3d7ab7d8b39958e8853f9e1371159499a0c3bf2',
    'src/texture.rs': 'fa6981a1cd44bd2dd56378c2e33c19964d235eeac0ccee37eba82054c09a52d1',
    'src/tcoef_tables.rs': '9c1279b6c4c7edba0163570ae448a41d9ad623cf176ee506656fb98511985912',
    'src/predictor.rs': '48d349cd55d0828aadad416dc8d4e4c32ac9298791deae14a930d6df997003fd',
}


def integer_expression(expression, quantiser):
    """Evaluate only integer arithmetic used by the pinned scaler expressions."""
    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name) and node.id == 'quantiser_scale':
            return quantiser
        if isinstance(node, ast.BinOp):
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Div) and left >= 0 and right > 0:
                return left // right
        raise ValueError('unsupported scaler expression: ' + expression)
    return visit(ast.parse(expression, mode='eval').body)


def rust_rows(source, name, width):
    match = re.search(r'\bconst\s+' + name + r'\s*:.*?=\s*&\[(.*?)\];', source, re.S)
    if not match:
        raise ValueError('missing upstream table: ' + name)
    body = re.sub(r'//[^\n]*', '', match[1])
    rows = []
    for row in re.findall(r'\(([^()]*)\)', body):
        fields = [value.strip().replace('_', '') for value in row.split(',') if value.strip()]
        if len(fields) != width:
            raise ValueError('wrong tuple width in ' + name)
        rows.append([int(value, 0) for value in fields])
    return rows


def extract(directory):
    sources = {}
    for name, expected in HASHES.items():
        content = (directory / name).read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise ValueError('pinned source hash mismatch: ' + name)
        sources[name] = content.decode('utf-8')
    data = {'source': {'repository': UPSTREAM, 'revision': REVISION,
                       'license': 'MIT', 'copyright': 'Copyright (c) 2026 Karpelès Lab Inc.',
                       'sha256': HASHES}, 'dc': {}}
    for component, name in [('lum', 'DC_SIZE_LUMINANCE'), ('chrom', 'DC_SIZE_CHROMINANCE')]:
        rows = rust_rows(sources['src/texture.rs'], name, 3)
        data['dc'][component] = [{'bits': format(code, '0%db' % length), 'size': size}
                                 for code, length, size in rows]
    data['intra'] = [{'bits': format(code, '0%db' % length), 'last': last,
                      'run': run, 'level': level}
                     for code, length, last, run, level in
                     rust_rows(sources['src/tcoef_tables.rs'], 'TCOEF_INTRA', 5)]
    # Preserve the published source row order in JSON. The C header uses the
    # decoder's (LAST, RUN, magnitude) order; no upstream lookup builder is used.
    data['scale_rules'] = {}
    for component, rust_component in [('lum', 'Luminance'), ('chrom', 'Chrominance')]:
        match = re.search(r'DcComponent::' + rust_component +
                          r'\s*=>\s*match quantiser_scale\s*\{([^}]+)\}',
                          sources['src/predictor.rs'])
        if not match:
            raise ValueError('missing upstream scaler rules')
        rules = []
        for low, high, wildcard, expression in re.findall(
                r'(?:(\d+)\.\.=([0-9]+)|(_))\s*=>\s*([^,]+),', match[1]):
            lower = rules[-1]['max'] + 1 if wildcard else int(low)
            rules.append({'min': lower, 'max': 31 if wildcard else int(high),
                          'expression': expression.strip()})
        data['scale_rules'][component] = rules
    return data


def prefix_check(words):
    for index, bits in enumerate(words):
        if not re.fullmatch('[01]{1,12}', bits):
            raise ValueError('invalid codeword')
        if any(other.startswith(bits) for other in words[index + 1:]):
            raise ValueError('codeword prefix collision')
        if any(bits.startswith(other) for other in words[index + 1:]):
            raise ValueError('codeword prefix collision')


def validate(data):
    expected_source = {'repository': UPSTREAM, 'revision': REVISION,
                       'license': 'MIT', 'copyright': 'Copyright (c) 2026 Karpelès Lab Inc.',
                       'sha256': HASHES}
    if data['source'] != expected_source:
        raise ValueError('canonical source metadata differs from pinned source')
    for rows in data['dc'].values():
        if len(rows) != 13 or sorted(row['size'] for row in rows) != list(range(13)):
            raise ValueError('DC table must cover sizes 0..12 exactly')
        prefix_check([row['bits'] for row in rows])
    events = data['intra']
    meanings = {(row['last'], row['run'], row['level']) for row in events}
    if len(events) != 102 or len(meanings) != 102 or sum(row['last'] == 0 for row in events) != 67:
        raise ValueError('wrong intra event count, meanings or LAST split')
    if any(last not in (0, 1) or not 0 <= run <= 63 or not 1 <= level <= 127
           for last, run, level in meanings):
        raise ValueError('intra event out of range')
    prefix_check([row['bits'] for row in events] + ['0000011'])
    for rules in data['scale_rules'].values():
        if [q for rule in rules for q in range(rule['min'], rule['max'] + 1)] != list(range(1, 32)):
            raise ValueError('scaler rules must cover valid quantisers 1..31 exactly')
        for rule in rules:
            for q in range(rule['min'], rule['max'] + 1):
                if not 1 <= integer_expression(rule['expression'], q) < 256:
                    raise ValueError('scaler out of range')


def lookup(events):
    result = [0xffff] * 4096
    for symbol, row in enumerate(events):
        bits = row['bits']
        start = int(bits, 2) << (12 - len(bits))
        for prefix in range(start, start + (1 << (12 - len(bits)))):
            if result[prefix] != 0xffff:
                raise ValueError('intra lookup collision')
            result[prefix] = (len(bits) << 8) | symbol
    # Independently check all integer-built entries by textual prefix matching.
    for prefix, actual in enumerate(result):
        word = format(prefix, '012b')
        matches = [(len(row['bits']) << 8) | symbol
                   for symbol, row in enumerate(events) if word.startswith(row['bits'])]
        if actual != (matches[0] if matches else 0xffff) or len(matches) > 1:
            raise ValueError('intra lookup prefix check failed')
    return result


def render(data):
    validate(data)
    events = sorted(data['intra'], key=lambda row: (row['last'], row['run'], row['level']))
    lines = ['/* SPDX-License-Identifier: MIT',
             ' * Generated by tools/gen_m4v_tables.py. Do not edit numeric arrays.',
             ' * MIT numeric input: OxideAV/oxideav-mpeg4video, revision',
             ' * ' + REVISION + '.',
             ' * Copyright (c) 2026 Karpelès Lab Inc.',
             ' * Retain docs/licenses/third-party-code-MIT.txt; see',
             ' * docs/M4V_TABLE_PROVENANCE.md for input hashes and regeneration.',
             ' * The 12-bit lookup packs (length << 8) | symbol, or 0xffff when',
             ' * no event matches. Escape is decoded separately.',
             ' */', '#ifndef NJ_M4V_TABLES_H', '#define NJ_M4V_TABLES_H', '',
             '#include "nj2me/nj.h"', '', '#define NJ_M4V_AC_BITS   12',
             '#define NJ_M4V_AC_SYMS   102', '#define NJ_M4V_AC_LAST   67u', '',
             'typedef struct nj_m4v_dc_entry {', '    nj_u2 code;',
             '    nj_u1 len;', '    nj_u1 size;', '} nj_m4v_dc_entry;', '']
    for component in ('lum', 'chrom'):
        lines.append('static const nj_m4v_dc_entry nj_m4v_dc_%s[13] = {' % component)
        for row in data['dc'][component]:
            lines.append('    { 0x%x, %d, %d },' % (int(row['bits'], 2), len(row['bits']), row['size']))
        lines += ['};', '']

    def array(ctype, name, values, width=16, hexdigits=False, dimension=None):
        lines.append('static const %s %s[%s] = {' % (ctype, name, dimension or len(values)))
        for offset in range(0, len(values), width):
            lines.append('    ' + ', '.join(('0x%04x' % value) if hexdigits else str(value)
                                           for value in values[offset:offset+width]) + ',')
        lines.extend(['};', ''])
    for field in ('run', 'level'):
        array('nj_u1', 'nj_m4v_ac_' + field, [row[field] for row in events], dimension='NJ_M4V_AC_SYMS')
    array('nj_u2', 'nj_m4v_ac_lut', lookup(events), width=12, hexdigits=True)
    lines += ['/* Table 7-1 scaler expressions evaluated for quantisers 1..31.',
              ' * Index zero is a local unused sentinel, not an upstream valid quantiser. */']
    for component in ('lum', 'chrom'):
        values = [0]
        for rule in data['scale_rules'][component]:
            values += [integer_expression(rule['expression'], q) for q in range(rule['min'], rule['max'] + 1)]
        array('nj_u1', 'nj_m4v_dc_scale_' + component, values)
    return '\n'.join(lines + ['#endif', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', type=Path, help='Read-only upstream checkout at the pinned revision')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = extract(args.extract) if args.extract else json.loads(DATA.read_text())
    validate(data)
    canonical = json.dumps(data, ensure_ascii=False, indent=2) + '\n'
    output = render(data)
    if args.check:
        if args.extract and DATA.read_text() != canonical:
            raise SystemExit('canonical MPEG-4 data require regeneration')
        if OUTPUT.read_text() != output:
            raise SystemExit('MPEG-4 header requires regeneration')
    else:
        if args.extract:
            DATA.parent.mkdir(parents=True, exist_ok=True)
            DATA.write_text(canonical)
        OUTPUT.write_text(output)
    print('PASS: pinned MIT MPEG-4 tables; 102 events, DC sizes, scaler ranges, all 4096 prefixes' +
          ('; generated files current' if args.check else '; files regenerated'))


if __name__ == '__main__':
    main()
