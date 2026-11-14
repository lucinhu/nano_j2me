#!/usr/bin/env python3
"""Rebuild 44 AMR data definitions and the SID table from pinned OpenCORE data.

Generator code: MIT, as described in the root LICENSE.
OpenCORE input data: Apache-2.0; see docs/AMR_DATA_LICENSES.md and bundled notices.
No FFmpeg source is read. Default output is stdout; --write updates the header.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = 'opencore/codecs_v2/audio/gsm_amr/amr_nb/'
REVISION = '3b67218fb8efb776bcd79e7445774e02d778321d'
MANIFEST = ROOT / 'tools/data/amr-opencore.json'
TARGET = ROOT / 'src/midp/nj_amr_tables.h'
SID_TARGET = ROOT / 'src/midp/nj_amr_dtx.h'
MANIFEST_SHA256 = '5b089192e6daf64e3fc25331481289409b56156858db3c3d7206584b52515880'
SOURCE_ARRAYS = {'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/bitno_tab.cpp': {'arrays': ['bitno_MR475',
                                                                                 'bitno_MR515',
                                                                                 'bitno_MR59',
                                                                                 'bitno_MR67',
                                                                                 'bitno_MR74',
                                                                                 'bitno_MR795',
                                                                                 'bitno_MR102',
                                                                                 'bitno_MR122',
                                                                                 'bitno_MRDTX'],
                                                                      'sha256': '609b27cdb2792703d833f4939858d5fbe9fcc8f97fd5454095eb31ea607dab46'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/bitreorder_tab.cpp': {'arrays': ['reorderBits_MR475',
                                                                                      'reorderBits_MR515',
                                                                                      'reorderBits_MR59',
                                                                                      'reorderBits_MR67',
                                                                                      'reorderBits_MR74',
                                                                                      'reorderBits_MR795',
                                                                                      'reorderBits_MR102',
                                                                                      'reorderBits_MR122'],
                                                                           'sha256': '4b4c2ab8326ea5382cdfda02dfa1a2b1745303c6c05d27f144336da6e6e193ec'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/c2_9pf_tab.cpp': {'arrays': ['startPos'],
                                                                       'sha256': '1e341cd603f552d435bd61104c73a9a2fc22c2441f8142f17561da65a09edb50'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gains_tbl.cpp': {'arrays': ['qua_gain_code',
                                                                                 'qua_gain_pitch'],
                                                                      'sha256': '4453ba4fb31568e7494df68372db19d250d703c6ff2b46eb36ef6a2dc24c31c1'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gc_pred.cpp': {'arrays': ['pred'],
                                                                    'sha256': 'd1a78c0c13282b671a6805597d5380a10ae820c360f03874f55a81b6f63424c2'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gray_tbl.cpp': {'arrays': ['dgray'],
                                                                     'sha256': '455a07453d0b1acdb2be8c1c5d8ce85a1d882bb191e3f4c5d9e4bb5d716f1c95'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/lsp_tab.cpp': {'arrays': ['lsp_init_data'],
                                                                    'sha256': '342972b741cb61b7926568aae3234a5fa65c9d670f76ae483f3ff68f909ee1b2'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/ph_disp_tab.cpp': {'arrays': ['ph_imp_mid',
                                                                                   'ph_imp_low',
                                                                                   'ph_imp_low_MR795'],
                                                                        'sha256': '887a153b0554727c9c15c256cf71184a231679f629b64d0953a6db6ef6ab106a'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/pred_lt.cpp': {'arrays': ['inter_6_pred_lt'],
                                                                    'sha256': '0a49d7f512d065136cfe175a367ebab60c6f8fb9c8f4cff36993defabd9d6f44'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/q_plsf_3_tbl.cpp': {'arrays': ['dico1_lsf_3',
                                                                                    'mr795_1_lsf',
                                                                                    'dico2_lsf_3',
                                                                                    'dico3_lsf_3',
                                                                                    'mr515_3_lsf',
                                                                                    'mean_lsf_3',
                                                                                    'pred_fac_3',
                                                                                     'past_rq_init'],
                                                                         'sha256': '3ab12e9a16e13952f642b09755c9979569899b5bb0e0fb2596c2fd8bc4611d0a'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/q_plsf_5_tbl.cpp': {'arrays': ['dico1_lsf_5',
                                                                                    'dico2_lsf_5',
                                                                                    'dico3_lsf_5',
                                                                                    'dico4_lsf_5',
                                                                                    'dico5_lsf_5',
                                                                                    'mean_lsf_5'],
                                                                         'sha256': '8c89a6adec9395210e8566df8ff19b5a5197504de34a22432b591239cb1126f2'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/qua_gain_tbl.cpp': {'arrays': ['table_gain_highrates',
                                                                                    'table_gain_lowrates'],
                                                                         'sha256': 'c646f376d33563ede32b03618ad0d42d4142361d0de134a6d3755715f1518621'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/dec/src/post_pro.cpp': {'arrays': ['a', 'b'],
                                                                  'sha256': '1db8123de197fb0f0483ef87f4373366bbb69ad6df3bc36b61c154af8ab5734d'},
 'opencore/codecs_v2/audio/gsm_amr/amr_nb/dec/src/qgain475_tab.cpp': {'arrays': ['table_gain_MR475'],
                                                                      'sha256': '0953ffa7730348061cb621c63454561327c00ad385e2e846a43ed20bee6a7fda'}}

def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def extract_array(text, name):
    match = re.search(r'\b' + re.escape(name) + r'\s*\[[^;=]*?=\s*\{(.*?)\}',
                      strip_comments(text), re.S)
    if not match:
        raise ValueError('missing upstream array: ' + name)
    body = match[1]
    if re.sub(r'-?(?:0x[0-9A-Fa-f]+|\d+)|[\s,]', '', body):
        raise ValueError('unsupported nonnumeric initializer: ' + name)
    return [int(x, 16) if 'x' in x.lower() else int(x)
            for x in re.findall(r'-?(?:0x[0-9A-Fa-f]+|\d+)', body)]


def mode_means(text):
    means = {}
    for value, modes in re.findall(
            r'means_ener\s*=\s*([\d.]+)\s*=\s*\d+\s+Q14\s*\(([^)]+)\)', text):
        for mode in re.findall(r'MR\d+', modes):
            means[mode] = float(value)
    means['MR122'] = float(re.search(
        r'#define MEAN_ENER_MR122[^\n]*/\*\s*([\d.]+)', text)[1])
    return [means[k] for k in ['MR475', 'MR515', 'MR59', 'MR67', 'MR74',
                               'MR795', 'MR102', 'MR122']]


def import_upstream(directory):
    sources = {}
    for path, expected in SOURCE_ARRAYS.items():
        data = (directory / path).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected['sha256']:
            raise ValueError('upstream SHA-256 mismatch: ' + path)
        text = data.decode('utf-8')
        sources[path] = {'sha256': expected['sha256'], 'arrays': {
            name: extract_array(text, name) for name in expected['arrays']}}
    return {'schema': 1,
            'upstream': 'https://github.com/BelledonneCommunications/opencore-amr',
            'revision': REVISION, 'license': 'Apache-2.0', 'sources': sources,
            'modified': 'nano_j2me: numeric extraction only; generated output converts Q formats, selects columns, and maps packet bits to the local frame layout.',
            'facts': {'energy_mean': mode_means((directory / (BASE + 'common/src/gc_pred.cpp')).read_text())}}


def load_manifest():
    data = MANIFEST.read_bytes()
    if hashlib.sha256(data).hexdigest() != MANIFEST_SHA256:
        raise ValueError('AMR manifest SHA-256 mismatch')
    return json.loads(data)


def build(manifest):
    """Convert numeric inputs to local data; packet maps invert the upstream bit permutation.

Parameter-to-field names are this project's frame layout. Sorting by local field
offset retains its storage contract; all bit positions come from OpenCORE."""
    definitions = {}

    def get(file, name):
        return manifest['sources'][BASE + file]['arrays'][name]
    C, D = ('common/src/', 'dec/src/')

    def add(name, typ, dims, values, recipe, inputs=()):
        definitions['nj_amr_' + name] = {'type': typ, 'dims': dims, 'values': values, 'recipe': recipe, 'inputs': list(inputs)}

    def direct(name, typ, dims, file, array, transform=lambda x: x, recipe='identity'):
        add(name, typ, dims, transform(get(file, array)), recipe, [BASE + file + '#' + array])
    q15 = lambda v: [float(format(x / 32768, '.6g')) for x in v]
    q15f6 = lambda v: [round(x / 32768, 6) for x in v]
    lsf = lambda v: [float(format(x * 4000 / 16384, '.6g')) for x in v]
    direct('b60_sinc', 'float', '[61]', C + 'pred_lt.cpp', 'inter_6_pred_lt', q15, 'Q15 / 32768 rounded to 6 significant digits')
    add('base_five_table', 'nj_u1', '[128][3]', [d for i in range(128) for d in ((i // 25, i // 5 % 5, i % 5) if i < 125 else (0, 0, 0))], 'three base-five digits for 0..124, zero padding 125..127')
    add('energy_mean', 'float', '[8]', manifest['facts']['energy_mean'], 'documented mode mean energies in gc_pred.cpp', [BASE + C + 'gc_pred.cpp#mode-mean-comments'])
    direct('energy_pred_fac', 'float', '[4]', C + 'gc_pred.cpp', 'pred', lambda v: [round(x / 8192, 2) for x in reversed(v)], 'reverse Q13 / 8192 rounded to two decimals')
    direct('gains_4k75', 'nj_u2', '[512][2]', D + 'qgain475_tab.cpp', 'table_gain_MR475')
    for n, a, rows in [('gains_high', 'table_gain_highrates', 128), ('gains_low', 'table_gain_lowrates', 64)]:
        direct(n, 'nj_u2', f'[{rows}][2]', C + 'qua_gain_tbl.cpp', a, lambda v: [x for i, x in enumerate(v) if i % 4 < 2], 'first two columns of four')
    direct('gray_decode', 'nj_u1', '[8]', C + 'gray_tbl.cpp', 'dgray', lambda v: [5 * x for x in v], 'multiply inverse Gray positions by track stride five')
    a = get(D + 'post_pro.cpp', 'a')
    b = get(D + 'post_pro.cpp', 'b')
    for name, v, dims, recipe in [('highpass_gain', [b[0] / 8192], '', 'b[0]/8192'), ('highpass_poles', [-a[1] / 8192, -a[2] / 8192], '[2]', '-a[1:]/8192'), ('highpass_zeros', [b[1] / b[0], b[2] / b[0]], '[2]', 'b[1:]/b[0]')]:
        add(name, 'float', dims, v, recipe, [BASE + D + 'post_pro.cpp#a', BASE + D + 'post_pro.cpp#b'])
    for name, a in [('ir_filter_medium', 'ph_imp_mid'), ('ir_filter_strong', 'ph_imp_low'), ('ir_filter_strong_7k95', 'ph_imp_low_MR795')]:
        direct(name, 'float', '[NJ_AMR_SUBFRAME]', C + 'ph_disp_tab.cpp', a, q15f6, 'Q15 / 32768 rounded to six decimals')
    for n, a, rows, cols in [('lsf_3_1', 'dico1_lsf_3', 256, 3), ('lsf_3_1_7k95', 'mr795_1_lsf', 512, 3), ('lsf_3_2', 'dico2_lsf_3', 512, 3), ('lsf_3_3', 'dico3_lsf_3', 512, 4), ('lsf_3_3_5k15', 'mr515_3_lsf', 128, 4)]:
        direct(n, 'nj_s2', f'[{rows}][{cols}]', C + 'q_plsf_3_tbl.cpp', a)
    direct('lsf_3_mean', 'float', '[NJ_AMR_LPC_ORDER]', C + 'q_plsf_3_tbl.cpp', 'mean_lsf_3', lsf, 'frequency /16384 *4000 rounded to 6 significant digits')
    for i, rows in enumerate([128, 256, 256, 256, 64], 1):
        direct('lsf_5_' + str(i), 'nj_s2', f'[{rows}][4]', C + 'q_plsf_5_tbl.cpp', 'dico' + str(i) + '_lsf_5')
    direct('lsf_5_mean', 'float', '[NJ_AMR_LPC_ORDER]', C + 'q_plsf_5_tbl.cpp', 'mean_lsf_5', lsf, 'frequency /16384 *4000 rounded to 6 significant digits')
    direct('lsp_avg_init', 'nj_s2', '[NJ_AMR_LPC_ORDER]', C + 'q_plsf_5_tbl.cpp', 'mean_lsf_5')
    direct('lsp_sub4_init', 'signed char', '[NJ_AMR_LPC_ORDER]', C + 'lsp_tab.cpp', 'lsp_init_data', lambda v: [x // 1000 for x in v], 'divide initial Q15 samples by 1000 (all exact multiples)')
    mode_suffixes = ['4k75', '5k15', '5k9', '6k7', '7k4', '7k95', '10k2', '12k2']
    mode_ids = ['475', '515', '59', '67', '74', '795', '102', '122']
    for mi, (mode, suffix) in enumerate(zip(mode_ids, mode_suffixes)):
        sizes = get(C + 'bitno_tab.cpp', 'bitno_MR' + mode)
        perm = get(C + 'bitreorder_tab.cpp', 'reorderBits_MR' + mode)
        if len(perm) != sum(sizes) or sorted(perm) != list(range(len(perm))):
            raise ValueError('invalid upstream packet permutation: MR' + mode)
        inverse = [0] * len(perm)
        for packet, canonical in enumerate(perm):
            inverse[canonical] = packet ^ 7
        nlsf = 5 if mi == 7 else 3
        fields = [(0, i, f'NJ_AMR_OFF(lsf[{i}])') for i in range(nlsf)]
        for sub in range(4):
            if mi == 7:
                names = ['p_lag', 'p_gain'] + [f'pulses[{x}]' for x in [1, 3, 5, 7, 9, 0, 2, 4, 6, 8]] + ['fixed_gain']
            elif mi == 6:
                names = ['p_lag'] + [f'pulses[{i}]' for i in range(7)] + ['p_gain']
            elif mi == 5:
                names = ['p_lag', 'pulses[0]', 'pulses[1]', 'p_gain', 'fixed_gain']
            else:
                names = ['p_lag', 'pulses[0]', 'pulses[1]'] + (['p_gain'] if mi != 0 or sub % 2 == 0 else [])
            for name in names:
                off = {'p_lag': 0, 'p_gain': 1, 'fixed_gain': 2}.get(name)
                if off is None:
                    off = 3 + int(re.search('\\d+', name)[0])
                fields.append((sub + 1, off, f'NJ_AMR_OFF_SUB({sub},{name})'))
        if len(fields) != len(sizes):
            raise ValueError('local frame parameter count differs: MR' + mode)
        groups = []
        at = 0
        for (sub, offset, field), nbits in zip(fields, sizes):
            groups.append(((sub, offset), [nbits, field] + inverse[at:at + nbits]))
            at += nbits
        values = [x for _, g in sorted(groups) for x in g] + [0]
        add('order_' + suffix, 'nj_u1', '[]', values, 'inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets', [BASE + C + 'bitno_tab.cpp#bitno_MR' + mode, BASE + C + 'bitreorder_tab.cpp#reorderBits_MR' + mode])
    add('payload_bytes', 'nj_u1', '[NJ_AMR_MODES]', [(sum(get(C + 'bitno_tab.cpp', 'bitno_MR' + m)) + 7) // 8 for m in mode_ids] + [(sum(get(C + 'bitno_tab.cpp', 'bitno_MRDTX')) + 7) // 8], 'ceil(parameter bits /8), including SID bitno_MRDTX', [BASE + C + 'bitno_tab.cpp#bitno_MR' + m for m in mode_ids + ['DTX']])
    for name, base in [('pow_0_55', 0.55), ('pow_0_7', 0.7), ('pow_0_75', 0.75)]:
        add(name, 'float', '[10]', [round(base ** i, 6) for i in range(1, 11)], f'round({base} ** i, 6) for i=1..10')
    direct('pred_fac', 'float', '[NJ_AMR_LPC_ORDER]', C + 'q_plsf_3_tbl.cpp', 'pred_fac_3', q15f6, 'Q15 /32768 rounded to six decimals')
    direct('qua_gain_code', 'nj_u2', '[32]', C + 'gains_tbl.cpp', 'qua_gain_code', lambda v: v[:96:3], 'first column of 33x3, omit sentinel final row')
    direct('qua_gain_pit', 'nj_u2', '[16]', C + 'gains_tbl.cpp', 'qua_gain_pitch')
    direct('track_position', 'nj_u1', '[16]', C + 'c2_9pf_tab.cpp', 'startPos')
    if len(definitions) != 44:
        raise ValueError('expected all 44 AMR definitions')
    return definitions


def build_sid(manifest):
    """Keep the 80 upstream signed Q15 residuals unchanged, in eight rows."""
    source = BASE + 'common/src/q_plsf_3_tbl.cpp'
    values = manifest['sources'][source]['arrays']['past_rq_init']
    if len(values) != 80 or any(type(x) is not int or not -32768 <= x <= 32767
                                for x in values):
        raise ValueError('SID past_rq_init must contain 80 signed Q15 values')
    return {'nj_amr_sid_reference': {
        'type': 'nj_s2', 'dims': '[8][10]', 'values': list(values),
        'recipe': 'identity signed Q15 values; reshape 80 entries into eight rows of ten',
        'inputs': [source + '#past_rq_init']}}


SID_HEADER = """/*
 * nano_j2me - AMR-NB SID reference vectors (numeric data only).
 * Apache-2.0 data from OpenCORE revision
 * 3b67218fb8efb776bcd79e7445774e02d778321d,
 * common/src/q_plsf_3_tbl.cpp#past_rq_init.
 * Copyright (C) 1998-2009 PacketVideo
 * (C) 2004, 3GPP Organizational Partners (ARIB, ATIS, CCSA, ETSI, TTA, TTC)
 * See docs/licenses/AMR-Apache-2.0.txt and
 * docs/licenses/AMR-OpenCORE-NOTICE.txt for license and attribution.
 * Modification: reshape 80 signed Q15 frequency residuals into [8][10];
 * values are unchanged. Runtime algorithms are independently implemented.
 */
#ifndef NJ_AMR_DTX_H
#define NJ_AMR_DTX_H
"""


def render_sid(definitions):
    entry = definitions['nj_amr_sid_reference']
    out = [SID_HEADER, 'static const nj_s2 nj_amr_sid_reference[8][10] = {\n']
    rows = [entry['values'][i:i + 10] for i in range(0, 80, 10)]
    out.append(',\n'.join('    {' + ','.join(str(x) for x in row) + '}'
                           for row in rows))
    out.append('\n};\n#endif\n')
    return ''.join(out)


HEADER = """/*
 * nano_j2me - generated AMR-NB data; tools/gen_amr_tables.py
 * Data derived from OpenCORE 3b67218fb8efb776bcd79e7445774e02d778321d.
 * Copyright (C) 1998-2009 PacketVideo
 * Copyright (C) 1998-2010 PacketVideo (post_pro.cpp data)
 * (C) 2004, 3GPP Organizational Partners (ARIB, ATIS, CCSA, ETSI, TTA, TTC)
 * Licensed under the Apache License, Version 2.0. Full license and attribution:
 * docs/licenses/AMR-Apache-2.0.txt and docs/licenses/AMR-OpenCORE-NOTICE.txt.
 * Modified by nano_j2me: extract numeric arrays, convert Q formats/select columns,
 * and reconstruct bit maps for the local frame layout; no upstream decoder code.
 * Formula-only arrays are original MIT generator output; see per-array comments.
 * See docs/AMR_DATA_LICENSES.md for every input, transformation and limitation.
 */
#ifndef NJ_AMR_TABLES_H
#define NJ_AMR_TABLES_H
#if !defined(NJ_AMR_LPC_ORDER) || !defined(NJ_AMR_SUBFRAME)
#  error "include AMR constants before nj_amr_tables.h"
#endif

"""


def number(value, typ):
    if isinstance(value, str):
        return value
    if typ == 'float':
        return repr(float(value)) + 'f'
    return str(value)


def render(definitions):
    out = [HEADER]
    for name, d in sorted(definitions.items()):
        license_name = 'Apache-2.0 data' if d['inputs'] else 'MIT formula data'
        out.append('/* ' + license_name + ': ' + d['recipe'] + '. */\n')
        for source in d['inputs']:
            out.append('/* Input: ' + source.removeprefix(BASE) + ' */\n')
        prefix = 'static const ' + d['type'] + ' ' + name + d['dims'] + ' = '
        values = d['values']
        if not d['dims']:
            out.append(prefix + number(values[0], d['type']) + ';\n\n')
        elif name.startswith('nj_amr_order_'):
            out.append(prefix + '{\n')
            i = 0
            while values[i]:
                count = values[i]
                group = values[i:i + count + 2]
                out.append('    ' + ', '.join(number(x, d['type']) for x in group) + ',\n')
                i += count + 2
            out.append('    0\n};\n\n')
        else:
            out.append(prefix + '{\n')
            dimensions = re.findall(r'\[(\d+)\]', d['dims'])
            width = int(dimensions[1]) if len(dimensions) == 2 else 8
            for i in range(0, len(values), width):
                chunk = ', '.join(number(x, d['type']) for x in values[i:i + width])
                if len(dimensions) == 2:
                    chunk = '{ ' + chunk + ' }'
                out.append('    ' + chunk + ',\n')
            out.append('};\n\n')
    out.append('#endif /* NJ_AMR_TABLES_H */\n')
    return ''.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--import-opencore', type=Path, metavar='CHECKOUT')
    parser.add_argument('--verify-upstream', type=Path, metavar='CHECKOUT')
    parser.add_argument('--sid', action='store_true',
                        help='select the independent SID header for stdout or --write; --check always checks both')
    args = parser.parse_args()
    try:
        if args.import_opencore:
            print(json.dumps(import_upstream(args.import_opencore), indent=2, sort_keys=True))
            return 0
        manifest = load_manifest()
        if args.verify_upstream and import_upstream(args.verify_upstream) != manifest:
            raise ValueError('pinned upstream numeric inputs differ from manifest')
        generated = render(build(manifest))
        sid_generated = render_sid(build_sid(manifest))
        if args.check:
            for target, expected in [(TARGET, generated), (SID_TARGET, sid_generated)]:
                if target.read_text() != expected:
                    raise ValueError('generated AMR header differs: ' + str(target))
            print('PASS: all 44 AMR definitions reproduce the complete header from Apache data / MIT formulas')
            print('PASS: independent SID header reproduces all 80 signed Q15 values from pinned past_rq_init')
        elif args.write:
            target, expected = (SID_TARGET, sid_generated) if args.sid else (TARGET, generated)
            target.write_text(expected)
            print('Wrote ' + str(target))
        else:
            print(sid_generated if args.sid else generated, end='')
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
