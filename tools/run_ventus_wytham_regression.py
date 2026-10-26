#!/usr/bin/env python3
"""VENTUS v0.1 CLI parameter regression runner (stdlib only).

Works with the CSV-only standalone ventus-cli. Does not invoke CloudCompare.
Usage:
 python3 run_ventus_wytham_regression.py --target target.csv \
    --obstacle neighbours.csv --cli ./build/ventus-cli --out tests/results/wytham
"""
import argparse
import csv
import math
import subprocess
import sys
from pathlib import Path


def one_row(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise RuntimeError(f'Expected one crown row in {path}; got {len(rows)}')
    return rows[0]


def read_points(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise RuntimeError(f'Empty point output: {path}')
    return rows


def assert_aligned(base, compared):
    if len(base) != len(compared):
        raise AssertionError('Target point count changed across runs')
    for i, (a, b) in enumerate(zip(base, compared)):
        for key in ('x', 'y', 'z'):
            if a[key] != b[key]:
                raise AssertionError(f'Point order/coordinates changed at row {i} ({key})')


def near(a, b, tol=2e-8):
    return math.isclose(float(a), float(b), rel_tol=tol, abs_tol=tol)


def assert_nonincreasing(previous, current, column, label, tol=2e-8):
    assert_aligned(previous, current)
    violations = [i for i, (old, new) in enumerate(zip(previous, current))
                  if float(new[column]) > float(old[column]) + tol]
    if violations:
        raise AssertionError(f'{label}: {len(violations)} points increased, first index {violations[0]}')
    return f'PASS {label} ({len(current)} points)'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--target', required=True, type=Path, help='XYZ CSV exported from Wytham target')
    p.add_argument('--obstacle', action='append', type=Path, default=[], help='XYZ CSV; repeat for multiple neighbour clouds')
    p.add_argument('--cli', type=Path, default=Path('./build/ventus-cli'))
    p.add_argument('--out', type=Path, default=Path('tests/results/wytham-sweep'))
    p.add_argument('--direction', type=float, default=270)
    p.add_argument('--speed', type=float, default=5)
    p.add_argument('--range', dest='distance', type=float, default=25)
    p.add_argument('--rose', type=Path, help='Optional wind rose CSV; overrides --direction and --speed')
    p.add_argument('--azimuth-samples', type=int, default=16)
    args = p.parse_args()
    cli = args.cli.expanduser().resolve()
    if not cli.is_file():
        p.error(f'Cannot find executable: {cli}. Build with cmake first.')
    target = args.target.expanduser().resolve()
    if not target.is_file():
        p.error(f'Target file not found: {target}')
    obstacles = [o.expanduser().resolve() for o in args.obstacle]
    if any(not o.is_file() for o in obstacles):
        p.error('One or more obstacle files do not exist')
    rose = args.rose.expanduser().resolve() if args.rose else None
    if rose and not rose.is_file():
        p.error(f'Wind rose not found: {rose}')
    out = args.out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = []
    def run(label, voxel, attenuation, elevation, context=False):
        prefix = out / label
        cmd = [str(cli), '--target', str(target), '--voxel', str(voxel),
               '--range', str(args.distance), '--attenuation', str(attenuation),
               '--azimuth-samples', str(args.azimuth_samples),
               '--elevation-samples', str(elevation), '--output-prefix', str(prefix)]
        if rose:
            cmd.extend(['--rose', str(rose)])
        else:
            cmd.extend(['--direction', str(args.direction), '--speed', str(args.speed)])
        if context:
            for path in obstacles:
                cmd.extend(['--obstacle', str(path)])
            cmd.append('--compare-neighbours')
        print(f'RUN {label}', flush=True)
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        crown = one_row(Path(str(prefix) + '-crown.csv'))
        records.append({
            'case': label, 'voxel_m': voxel, 'attenuation_per_m': attenuation,
            'elevation_samples': elevation, 'context': int(context),
            **{key: crown[key] for key in (
                'CWEI', 'WEF', 'PressureWeightedExposure', 'PressureRatio',
                'SpeedRatio', 'MeanOccupiedUpwindPath_m',
                'MaxProjectedArea_m2', 'NeighbourShelterDeltaWEI')}
        })
        return read_points(Path(str(prefix) + '-points.csv'))

    checks = []
    try:
        atten = [(0.0, 'k0'), (0.15, 'k015'), (0.5, 'k05'), (1.0, 'k1')]
        att = {k: run(name, .5, k, 3) for k, name in atten}
        baseline = att[0.0]
        for k, _ in atten:
            assert_aligned(baseline, att[k])
            for a, b in zip(baseline, att[k]):
                for key in ('WEF', 'WEI', 'DWE', 'WPWE'):
                    if not near(a[key], b[key]):
                        raise AssertionError(f'{key} changed with attenuation k={k}')
            checks.append(f'PASS Level 1 invariance at k={k}')
        for i, a in enumerate(baseline):
            if not near(a['PressureRatio'], 1.0) or not near(a['SpeedRatio'], 1.0):
                raise AssertionError(f'At k=0 ratios should equal 1; point {i}')
        checks.append(f'PASS k=0 point ratios equal 1 ({len(baseline)} points)')
        baseline_crown = next(row for row in records if row['case'] == 'k0')
        for key in ('PressureRatio', 'SpeedRatio'):
            if not near(baseline_crown[key], 1.0):
                raise AssertionError(f'At k=0 crown {key} should be exactly 1; got {baseline_crown[key]}')
        checks.append('PASS k=0 crown ratios equal 1')
        for (before, _), (after, _) in zip(atten, atten[1:]):
            checks.append(assert_nonincreasing(att[before], att[after], 'PressureRatio',
                                               f'PressureRatio k={before} -> k={after}'))
            checks.append(assert_nonincreasing(att[before], att[after], 'SpeedRatio',
                                               f'SpeedRatio k={before} -> k={after}'))
        for voxel in (.25, .5, 1.0):
            if voxel != .5:
                run('voxel_' + str(voxel).replace('.', 'p'), voxel, 1.0, 3)
        for elevation in (7, 11):
            run(f'elev_{elevation}', .5, 1.0, elevation)
        if obstacles:
            with_context = run('context_k1', .5, 1.0, 3, context=True)
            for key in ('WEF', 'WEI', 'PressureRatio', 'SpeedRatio'):
                checks.append(assert_nonincreasing(att[1.0], with_context, key,
                                                   f'Add neighbours: {key}'))
        with (out / 'summary.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
        (out / 'checks.txt').write_text('\n'.join(checks) + '\n')
        print('\n'.join(checks))
        print('\nCrown summary:')
        print(f'{"case":<20} {"CWEI":>9} {"pressure":>12} {"path m":>10}')
        for row in records:
            print(f'{row["case"]:<20} {float(row["CWEI"]):>9.4f} {float(row["PressureRatio"]):>12.4f} '
                  f'{float(row["MeanOccupiedUpwindPath_m"]):>10.3f}')
        print(f'\nSaved: {out / "summary.csv"} and {out / "checks.txt"}')
    except (AssertionError, subprocess.CalledProcessError, RuntimeError) as exc:
        print(f'REGRESSION FAILED: {exc}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
