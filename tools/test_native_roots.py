#!/usr/bin/env python3
"""Offline production roots and SDK comparison; never a device or socket."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_SHA = '7101770dc6db2667b3c477cc31365dd1acd6db4e'
MBED_SHA = '6cc42afad309e861f4c07e6f106e2ab14a9cb8e5'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
    parser.add_argument('--output', type=Path, required=True, help='New isolated output directory')
    args = parser.parse_args()
    if not args.idf_path:
        parser.error('--idf-path or IDF_PATH is required')
    sdk = args.idf_path.resolve()
    mbed = sdk / 'components/mbedtls/mbedtls'
    out = args.output.resolve()
    if out.exists():
        parser.error('--output must not exist; earlier evidence is never overwritten')
    out.mkdir(parents=True)
    rows = []

    def run(argv, expected=0):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=out, capture_output=True, text=True)
        with (out / 'commands.log').open('a') as log:
            log.write(repr(argv) + '\n' + p.stdout + p.stderr + '\n')
        if p.returncode != expected:
            raise RuntimeError(f'{argv[0]} exited {p.returncode}, expected {expected}; see commands.log')
        return p.stdout

    for path, sha in [(sdk, SDK_SHA), (mbed, MBED_SHA)]:
        if run(['git', '-C', path, 'rev-parse', 'HEAD']).strip() != sha:
            raise RuntimeError('SDK revision differs from the reviewed release pin')
        if run(['git', '-C', path, 'status', '--porcelain', '--untracked-files=no']).strip():
            raise RuntimeError('SDK tracked sources must be clean')
    fixtures = ROOT / 'tools/native_roots/fixtures'
    manifest = json.loads((fixtures / 'sha256.json').read_text())
    expected_names = {'braiins.com-full.pem', 'braiins.com-short.pem', 'public-pool.io-full.pem',
                      'attacker-full.pem', 'bad-signature.pem', 'self.pem'}
    if set(manifest) != expected_names or {f.name for f in fixtures.glob('*.pem')} != expected_names:
        raise RuntimeError('Expected exactly the six documented certificate fixtures')
    for name, digest in manifest.items():
        if hashlib.sha256((fixtures / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Fixture checksum mismatch: {name}')
    bundle = ROOT / 'certificates/cacert-2026-10-20.pem'
    if hashlib.sha256(bundle.read_bytes()).hexdigest() != bundle.with_suffix('.pem.sha256').read_text().split()[0]:
        raise RuntimeError('Pinned CA checksum mismatch')
    incident_fixtures = out / 'incident-fixtures'
    incident_fixtures.mkdir()
    archive = ROOT / 'certificates/test/emudhra-chains-2026-10-20.zip'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != '8a2edc81b6c51e3ad27d45ca1e441ec84c961e2dee0fc547f2a02c0f74315f95':
        raise RuntimeError('Incident archive checksum mismatch')
    with zipfile.ZipFile(archive) as chains:
        names = [name for name in chains.namelist() if name.endswith('.pem')]
        assert len(names) == 12
        for name in names:
            target = incident_fixtures / Path(name).name
            assert not target.exists()
            target.write_bytes(chains.read(name))
    run([sys.executable, ROOT / 'tools/generate_pinned_roots.py', '--output', out / 'roots.inc'])
    config = out / 'native_config.h'
    # Match firmware's inherited time policy; archived incident leaves can expire.
    config.write_text('#undef MBEDTLS_HAVE_TIME_DATE\n#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK\n')
    compat = out / 'compat/mbedtls'
    compat.mkdir(parents=True)
    for header in (mbed / 'tf-psa-crypto/drivers/builtin/include/mbedtls/private').glob('*.h'):
        (compat / header.name).write_text('#include "' + str(header) + '"\n')
    includes = [mbed / 'include', mbed / 'tf-psa-crypto/include',
                mbed / 'tf-psa-crypto/drivers/builtin/include', compat.parent,
                ROOT / 'components/pinned_roots', ROOT / 'tools/native_roots', out]
    flags = ['-DMBEDTLS_THREADING_C', '-DMBEDTLS_THREADING_PTHREAD',
             '-DMBEDTLS_PLATFORM_MEMORY', '-DMBEDTLS_USER_CONFIG_FILE="' + str(config) + '"']
    cflags = ' '.join(shlex.quote(x) for x in [*['-I' + str(p) for p in includes], *flags])
    run(['cmake', '-S', mbed, '-B', out / 'build', '-DENABLE_TESTING=OFF',
         '-DENABLE_PROGRAMS=OFF', '-DGEN_FILES=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
         '-DCMAKE_C_FLAGS=' + cflags])
    run(['cmake', '--build', out / 'build', '--parallel', '8'])
    libs = [out / 'build/library' / ('lib' + n + '.a') for n in ['mbedtls', 'mbedx509', 'mbedcrypto']]
    text = (sdk / 'components/mbedtls/esp_crt_bundle/esp_crt_bundle.c').read_text()
    for name in ['esp_check.h', 'esp_crt_bundle.h', 'esp_log.h', 'sdkconfig.h']:
        text = text.replace('#include "' + name + '"', '')
    (out / 'sdk_bundle.c').write_text((ROOT / 'tools/native_roots/bundle_shim.h').read_text() + text)
    def compile_probe(source, name, mode, extra_flags=()):
        extra = list(extra_flags)
        if mode == 'cross-sign':
            extra += ['-DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=1']
        if mode == 'custom':
            extra += ['-DCUSTOM_ROOTS', str(ROOT / 'components/pinned_roots/real_roots.c')]
        run([os.environ.get('CC', 'cc'), '-O2', '-pthread', *flags,
             *['-I' + str(p) for p in includes], '-I' + str(mbed / 'library'),
             *extra, ROOT / 'tools/native_roots' / source, *libs, '-o', out / name])
    for mode in ['stock', 'cross-sign', 'custom']:
        compile_probe('baseline.c', mode, mode)
        compile_probe('lifecycle.c', 'lifecycle-' + mode, mode)
    run([sys.executable, sdk / 'components/mbedtls/esp_crt_bundle/gen_crt_bundle.py', '--input', bundle])
    cases = [
        (fixtures / 'braiins.com-full.pem', 'braiins.com', 2, 0),
        (fixtures / 'braiins.com-short.pem', 'braiins.com', 0, 0),
        (fixtures / 'public-pool.io-full.pem', 'public-pool.io', 0, 0),
        (fixtures / 'braiins.com-short.pem', 'wrong.example', 2, 2),
        (fixtures / 'braiins.com-full.pem', 'wrong.example', 2, 2),
        (fixtures / 'public-pool.io-full.pem', 'wrong.example', 2, 2),
        *[(fixtures / name, 'braiins.com', 2, 2)
          for name in ['attacker-full.pem', 'bad-signature.pem', 'self.pem']],
        *[(f, f.stem, 2, 2) for f in sorted(incident_fixtures.glob('*.pem'))],
    ]
    assert len(cases) == 21
    for mode, index in [('stock', 2), ('cross-sign', 3), ('custom', 3)]:
        for case in cases:
            certificate, host = case[:2]
            expected = case[index]
            stdout = run([out / mode, out / 'x509_crt_bundle', certificate, host], expected)
            match = re.search(r'flags=0x([a-f0-9]+)\s*$', stdout)
            assert match is not None
            flags_out = int(match[1], 16)
            assert flags_out != 0 if expected else flags_out == 0
            if host == 'wrong.example' and (mode != 'stock' or certificate.name != 'braiins.com-full.pem'):
                assert flags_out & 4  # CN_MISMATCH, not merely missing trust.
            rows.append({'mode': mode, 'name': certificate.name, 'host': host,
                         'expected_exit': expected, 'output': stdout})
    # Cold PSA initialization is counted, and PSA is freed before checking final
    # ownership. Repeated calls happen IN ONE PROCESS, not once per process exit.
    for mode, index in [('stock', 2), ('cross-sign', 3), ('custom', 3)]:
        lifecycle_cases = cases if mode == 'custom' else cases[:3]
        for certificate, host, *expectations in lifecycle_cases:
            snapshots = []
            for loops in [1, 100]:
                leaking_control = mode == 'cross-sign'
                stdout = run([out / ('lifecycle-' + mode), out / 'x509_crt_bundle',
                              certificate, host, loops, expectations[index - 2]],
                             70 if leaking_control else 0)
                values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', stdout)}
                assert values['bad_results'] == 0
                if leaking_control:
                    assert values['remaining_allocations'] > 0 and values['sdk_bytes'] > 0
                    assert values['crypto_bytes'] == 0 and values['cross_domain_frees'] > 0
                else:
                    assert values['remaining_allocations'] == values['cross_domain_frees'] == 0
                    assert values['first_live'] == values['last_live']
                snapshots.append(values)
                rows.append({'lifecycle': mode, 'name': certificate.name, 'host': host,
                             'known_sdk_leak_control': leaking_control, 'output': stdout})
            if mode == 'cross-sign':
                assert snapshots[1]['sdk_bytes'] == 100 * snapshots[0]['sdk_bytes']
                assert snapshots[1]['remaining_allocations'] == 100 * snapshots[0]['remaining_allocations']
    # Keep production selection, failure-unwind and repeated-client coverage.
    for source, name in [('probe.c', 'fixed'), ('fault_probe.c', 'fault'),
                         ('selection_probe.c', 'selection')]:
        compile_probe(source, name, 'custom')
    rows.append({'selection': run([out / 'selection'])})
    for certificate, host, _, expected in cases[:3] + [cases[3], cases[7]]:
        rows.append({'fault': certificate.name, 'host': host,
                     'output': run([out / 'fault', certificate, host, expected])})
    for certificate, host, _, expected in cases[:3] + [cases[3]]:
        rows.append({'threads': certificate.name, 'host': host,
                     'output': run([out / 'fixed', certificate, host, '0', '0', expected, 'threads'])})
    # Preserve the original native-comparator, malformed/capped-root, mixed
    # failure/retry and time-policy checks with the new focused pool fixtures.
    sdk_text = (mbed / 'library/x509_crt.c').read_text()
    comparator = []
    for name in ['x509_memcasecmp', 'x509_string_cmp', 'x509_name_cmp']:
        start = sdk_text.index('static int ' + name + '(')
        pos = sdk_text.index('{', start) + 1
        depth = 1
        while depth:
            depth += (sdk_text[pos] == '{') - (sdk_text[pos] == '}')
            pos += 1
        comparator.append(sdk_text[start:pos])
    (out / 'native_comparator.inc').write_text('\n'.join(comparator))
    selection = (ROOT / 'tools/native_roots/selection_probe.c').read_text()
    selection = selection.replace('#include "real_roots.h"', '#include "real_roots.h"\n#include "native_comparator.inc"')
    selection = selection.replace('child.issuer=names[0];',
                                  'child.issuer=names[0];\nif(x509_name_cmp(&child.issuer,&root.subject)!=0)ret=-1;')
    native_selection = out / 'native_selection.c'
    native_selection.write_text(selection)
    compile_probe(native_selection, 'native-selection', 'custom', ['-DMBEDTLS_ALLOW_PRIVATE_ACCESS'])
    rows.append({'native_comparator': run([out / 'native-selection'])})
    raw = (out / 'roots.inc').read_text()
    helper = (ROOT / 'components/pinned_roots/real_roots.c').read_text()
    variants = {
        'empty': 'static const root_entry roots[1]={{NULL,0,NULL,0}};\n#define ROOT_COUNT 118\n#define ROOT_CANDIDATE_CAP 7\n#define ROOT_DER_CAP 6969\n',
        'corrupt': raw.replace('root_82[] = {48,', 'root_82[] = {0,', 1),
        'truncated': raw.replace('{root_82,sizeof(root_82),', '{root_82,sizeof(root_82)-1,', 1),
        'candidate-cap': raw.replace('#define ROOT_CANDIDATE_CAP 7', '#define ROOT_CANDIDATE_CAP 1'),
        'byte-cap': raw.replace('#define ROOT_DER_CAP 6969', '#define ROOT_DER_CAP 1'),
    }
    for name, data in variants.items():
        (out / (name + '.inc')).write_text(data)
        variant = out / (name + '.c')
        variant.write_text(helper.replace('#include "roots.inc"', '#include "' + name + '.inc"'))
        run([os.environ.get('CC', 'cc'), '-O2', '-pthread', *flags,
             *['-I' + str(p) for p in includes], ROOT / 'tools/native_roots/probe.c',
             variant, *libs, '-o', out / name])
        stdout = run([out / name, fixtures / 'braiins.com-full.pem', 'braiins.com', '0', '0', '2'], 2)
        assert 'residual=0' in stdout and 'lookup_error=-' in stdout
        rows.append({'variant': name, 'output': stdout})
    compile_probe('mixed_probe.c', 'mixed', 'custom')
    callback_fault = None
    for index in range(60, 150):
        argv = [str(out / 'fixed'), str(fixtures / 'braiins.com-full.pem'), 'braiins.com', str(index), '0', '2']
        p = subprocess.run(argv, cwd=out, capture_output=True, text=True)
        with (out / 'commands.log').open('a') as log:
            log.write(repr(argv) + '\n' + p.stdout + p.stderr + '\n')
        assert p.returncode in (0, 2) and 'residual=0' in p.stdout
        if 'lookup_error=-' in p.stdout:
            assert p.returncode == 2
            callback_fault = index
            break
    assert callback_fault is not None
    rows.append({'mixed_callback_fault': callback_fault,
                 'output': run([out / 'mixed', fixtures / 'braiins.com-full.pem', 'braiins.com', callback_fault])})
    time_config = out / 'time_config.h'
    time_config.write_text('#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK\n')
    time_flags = [x.replace(str(config), str(time_config)) for x in flags]
    time_cflags = ' '.join(shlex.quote(x) for x in [*['-I' + str(p) for p in includes], *time_flags])
    run(['cmake', '-S', mbed, '-B', out / 'time-build', '-DENABLE_TESTING=OFF',
         '-DENABLE_PROGRAMS=OFF', '-DGEN_FILES=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
         '-DCMAKE_C_FLAGS=' + time_cflags])
    run(['cmake', '--build', out / 'time-build', '--parallel', '8'])
    time_libs = [out / 'time-build/library' / ('lib' + n + '.a') for n in ['mbedtls', 'mbedx509', 'mbedcrypto']]
    run([os.environ.get('CC', 'cc'), '-O2', '-pthread', *time_flags,
         *['-I' + str(p) for p in includes], ROOT / 'tools/native_roots/time_probe.c',
         ROOT / 'components/pinned_roots/real_roots.c', *time_libs, '-o', out / 'time'])
    rows.append({'time_seams': run([out / 'time', fixtures / 'braiins.com-full.pem', 'braiins.com'])})
    (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(f'PASS: {len(rows)} recorded results; production custom roots leak-free, stock controls leak-free; pinned SDK cross-sign leak reproduced (NOT production-safe)')


if __name__ == '__main__':
    main()
