#!/usr/bin/env python3
"""Real CloudCompare BIN integration regression for VENTUS.

Requires installed CloudCompare with QVENTUS_PLUGIN. This tests actual plugin
registration, BIN read/write, target selection and metric monotonicity.
Use --list to discover exact entity names, then --target "...".
"""
import argparse
import csv
import math
import subprocess
import sys
from pathlib import Path


def cli(cc, binary, *args):
    command = [cc, '-SILENT', '-AUTO_SAVE', 'OFF', '-O', str(binary), '-VENTUS', *map(str, args)]
    print('+', ' '.join(map(str, command)), flush=True)
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'CloudCompare exited {result.returncode}:\n{result.stdout[-12000:]}')
    print(result.stdout[-3000:], flush=True)
    return result.stdout


def first_row(path):
    with open(path, newline='', encoding='utf-8-sig') as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 1:
        raise ValueError(f'Expected 1 crown summary in {path}, found {len(rows)}')
    return rows[0]


def number(row, *candidates):
    norm = {''.join(c.lower() for c in k if c.isalnum()): v for k, v in row.items()}
    for key in candidates:
        v = norm.get(''.join(c.lower() for c in key if c.isalnum()))
        if v is not None:
            n = float(v)
            if not math.isfinite(n):
                raise ValueError(f'Nonfinite metric {key}: {v}')
            return n
    raise KeyError(f'Missing {candidates}, CSV has {list(row)}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', required=True, help='Installed CloudCompare executable')
    p.add_argument('--bin', required=True, type=Path, help='Forest BIN file')
    p.add_argument('--target', help='Exact name of a cloud in the loaded BIN')
    p.add_argument('--list', action='store_true', help='List loaded clouds and exit')
    p.add_argument('--context', choices=('TARGET','ALL'), default='TARGET')
    p.add_argument('--direction', type=float, default=270)
    p.add_argument('--speed', type=float, default=5)
    p.add_argument('--voxel', type=float, default=0.5)
    p.add_argument('--range', type=float, default=25)
    p.add_argument('--out', type=Path, default=Path('build/ventus-bin-regression'))
    p.add_argument('--sweep', action='store_true', help='Run k=0,0.15,0.5,1.0 + numeric checks')
    p.add_argument('--points', action='store_true', help='Export very large point CSV too')
    args = p.parse_args()
    if not args.bin.is_file():
        p.error(f'Input BIN missing: {args.bin}')
    if args.list:
        cli(args.cc, args.bin, '-LIST')
        return
    if not args.target:
        p.error('Use --target "exact cloud name" or --list')
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    for k in (0.0, 0.15, 0.5, 1.0) if args.sweep else (0.15,):
        label = 'k' + str(k).replace('.', 'p')
        prefix = args.out / label
        csv_path = prefix.with_name(label + '-crown.csv')
        bin_path = prefix.with_name(label + '-VENTUS.bin')
        cmd = [
            '-TARGET', args.target, '-CONTEXT', args.context,
            '-DIRECTION', args.direction, '-SPEED', args.speed,
            '-VOXEL', args.voxel, '-RANGE', args.range,
            '-ATTENUATION', k, '-EXPORT_CSV', csv_path,
            '-EXPORT_BIN', bin_path, '-OVERWRITE',
        ]
        if args.points:
            cmd += ['-EXPORT_POINTS', prefix.with_name(label + '-points.csv')]
        cli(args.cc, args.bin, *cmd)
        if not csv_path.is_file() or csv_path.stat().st_size == 0:
            raise RuntimeError(f'Missing crown CSV: {csv_path}')
        if not bin_path.is_file() or bin_path.stat().st_size == 0:
            raise RuntimeError(f'Missing VENTUS BIN: {bin_path}')
        row = first_row(csv_path)
        records.append((k, number(row, 'CWEI'), number(row, 'pressureRatio')))
    print('\nk [1/m]         CWEI        Crown pressure ratio')
    for k, wei, pres in records:
        print(f'{k:7.2f}    {wei:10.6f}          {pres:10.6f}')
    if args.sweep:
        initial_cwei = records[0][1]
        if any(abs(r[1] - initial_cwei) > 1e-6 for r in records):
            raise AssertionError('CWEI should not vary with empirical attenuation')
        if abs(records[0][2] - 1.0) > 1e-6:
            raise AssertionError('At attenuation=0, crown pressure ratio should equal 1')
        if any(a[2] < b[2] - 1e-6 for a, b in zip(records, records[1:])):
            raise AssertionError('Pressure ratio increased with stronger attenuation')
        print('PASS: attenuation invariance and pressure-ratio monotonicity')
    print('PASS: Native BIN load/save and VENTUS CSV export')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, AssertionError, ValueError, KeyError) as e:
        print(f'VENTUS regression FAILED: {e}', file=sys.stderr)
        sys.exit(1)
