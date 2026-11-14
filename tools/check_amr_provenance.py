#!/usr/bin/env python3
"""Check complete AMR data provenance, generated output and PCM data contract.

MIT checker code. The checked OpenCORE numeric inputs remain Apache-2.0.
Optional --upstream verifies every numeric input against the pinned checkout.
Optional --baseline compares a previous header's typed values without importing
it into the generation path. Neither check requires a network connection.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('amr_generator', ROOT / 'tools/gen_amr_tables.py')
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)
# Filled from the existing header before replacing the old extraction route.
# This is an aggregate compatibility fingerprint, not generation input.
COMPAT_SHA256 = '379f403e3764de00ee6674f2673fb5c32cd44fe6322b65c729b05552818b3e26'
LICENSE_SHA256 = '8b3f1762349248d444ab9acbafe73941254e36e1064954da56bb9ddbd5873ddb'
NOTICE_SHA256 = '0b69e1b2ceccae05b6077a8874b1be6e36707ef0119a2b951b2c23fadf72309b'
FORMULA_NAMES = {'nj_amr_base_five_table', 'nj_amr_pow_0_55',
                 'nj_amr_pow_0_7', 'nj_amr_pow_0_75'}
BLOCK = re.compile(
    r'static const\s+([\w ]+?)\s+(nj_amr_\w+)((?:\[[^]]*\])*)\s*=\s*(\{.*?\}|[^;]+);', re.S)
TOKEN = re.compile(r'NJ_AMR_OFF(?:_SUB)?\([^)]*\)|-?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?')


def typed_header(text):
    result = {}
    for typ, name, dims, body in BLOCK.findall(GEN.strip_comments(text)):
        values = []
        for token in TOKEN.findall(body):
            if token.startswith('NJ_AMR_OFF'):
                values.append(token.replace(' ', ''))
            elif typ == 'float':
                values.append(struct.pack('<f', float(token)).hex())
            else:
                values.append(int(token))
        if name in result:
            raise ValueError('duplicate definition: ' + name)
        result[name] = {'type': typ, 'dims': dims, 'values': values}
    return result


def fingerprint(definitions):
    encoded = json.dumps(definitions, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def check(upstream=None, baseline=None):
    manifest = GEN.load_manifest()
    if manifest['license'] != 'Apache-2.0' or manifest['revision'] != GEN.REVISION:
        raise ValueError('unexpected AMR data license or upstream revision')
    if upstream is not None:
        if GEN.import_upstream(upstream) != manifest:
            raise ValueError('upstream numeric input does not match checked-in manifest')
    built = GEN.build(manifest)
    text = GEN.TARGET.read_text()
    if text != GEN.render(built):
        raise ValueError('AMR generated header does not reproduce byte for byte')
    actual = typed_header(text)
    if len(actual) != 44 or set(actual) != set(built):
        raise ValueError('incomplete AMR definition coverage')
    formulas = {name for name, entry in built.items() if not entry['inputs']}
    if formulas != FORMULA_NAMES:
        raise ValueError('unexpected formula-only AMR dataset')
    for name, entry in built.items():
        if not entry['recipe']:
            raise ValueError('missing data transformation description: ' + name)
        for source in entry['inputs']:
            path, array = source.split('#')
            record = manifest['sources'][path]
            if array != 'mode-mean-comments' and array not in record['arrays']:
                raise ValueError('untraceable upstream array: ' + source)
    if fingerprint(actual) != COMPAT_SHA256:
        raise ValueError('AMR typed values / dimensions differ from the preserved PCM contract')
    if baseline is not None and typed_header(baseline.read_text()) != actual:
        raise ValueError('AMR typed values differ from supplied baseline')
    sid = GEN.build_sid(manifest)
    sid_text = GEN.SID_TARGET.read_text()
    if sid_text != GEN.render_sid(sid):
        raise ValueError('AMR SID header does not reproduce byte for byte')
    sid_actual = typed_header(sid_text)
    sid_expected = {name: {key: entry[key] for key in ['type', 'dims', 'values']}
                    for name, entry in sid.items()}
    if sid_actual != sid_expected:
        raise ValueError('AMR SID type, dimensions or signed Q15 values differ')
    used_inputs = {source for entry in list(built.values()) + list(sid.values())
                   for source in entry['inputs'] if not source.endswith('#mode-mean-comments')}
    stored_inputs = {path + '#' + array for path, record in manifest['sources'].items()
                     for array in record['arrays']}
    if used_inputs != stored_inputs:
        raise ValueError('AMR numeric manifest has unmapped or missing source arrays')
    license_data = (ROOT / 'docs/licenses/AMR-Apache-2.0.txt').read_bytes()
    notice_data = (ROOT / 'docs/licenses/AMR-OpenCORE-NOTICE.txt').read_bytes()
    if hashlib.sha256(license_data).hexdigest() != LICENSE_SHA256:
        raise ValueError('full Apache license copy differs')
    if hashlib.sha256(notice_data).hexdigest() != NOTICE_SHA256:
        raise ValueError('AMR PacketVideo / 3GPP attribution copy differs')
    if upstream is not None and license_data != (upstream / 'LICENSE').read_bytes():
        raise ValueError('bundled Apache license differs from pinned upstream')
    print('PASS: 44/44 AMR definitions; 40 Apache-derived / 4 MIT formula datasets')
    print('PASS: independent SID header; 80 unchanged Q15 values from q_plsf_3_tbl.cpp#past_rq_init')
    print('PASS: 14 SHA-256-pinned upstream files; all numeric arrays mapped; full license and notices')
    print('PASS: all integer, binary32, macro-offset and dimension values preserve the PCM data contract')
    print('Typed-value SHA-256: ' + COMPAT_SHA256)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, metavar='CHECKOUT')
    parser.add_argument('--baseline', type=Path, metavar='PREVIOUS_HEADER')
    args = parser.parse_args()
    try:
        check(args.upstream, args.baseline)
        return 0
    except (OSError, KeyError, ValueError, struct.error) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
