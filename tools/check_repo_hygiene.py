#!/usr/bin/env python3
"""Reject large datasets / generated files from VENTUS's Git index.

Uses Git's staged or tracked path list and checks local file sizes. No files
are changed. Run after `git add -A`, before `git commit`.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LARGE_FILE_LIMIT = 5 * 1024 * 1024  # 5 MiB; all real scans should stay outside Git
BAD_SUFFIXES = {
    '.ply', '.bin', '.las', '.laz', '.e57', '.pcd', '.pts', '.ptx', '.xyz',
    '.xyzrgb', '.xyzn', '.sbf', '.asc', '.obj', '.stl', '.off', '.fbx',
    '.glb', '.gltf', '.npy', '.npz', '.h5', '.hdf5', '.tif', '.tiff',
    '.zip', '.7z', '.rar', '.tar', '.tgz', '.zst', '.gz'
}
BAD_DIRS = ('data/', 'datasets/', 'wytham/', 'cloudcompare_testdata/',
            'tests/data/', 'tests/results/', 'tests/output/',
            'results/', 'outputs/', 'output/', 'scratch/', 'tmp/')
ALLOWED_CSV = {'examples/tree_points.csv', 'examples/windbreak_points.csv',
               'examples/wind_rose.csv'}


def git_paths(mode: str) -> list[str]:
    if mode == 'tracked':
        args = ['git', 'ls-files', '-z']
    else:
        args = ['git', 'diff', '--cached', '--diff-filter=ACMR', '--name-only', '-z']
    out = subprocess.check_output(args, cwd=ROOT)
    return [item.decode('utf-8', errors='surrogateescape')
            for item in out.split(b'\0') if item]


def check(path_str: str) -> list[str]:
    p = pathlib.Path(path_str)
    posix = path_str.lower().replace('\\', '/')
    flags = []
    if any(posix.startswith(prefix) for prefix in BAD_DIRS) or '/wytham/' in posix:
        flags.append('dataset/results directory')
    if p.suffix.lower() in BAD_SUFFIXES:
        flags.append('binary point-cloud/scene/archive extension')
    if p.suffix.lower() == '.csv' and path_str not in ALLOWED_CSV and not posix.startswith('tests/fixtures/'):
        flags.append('generated or external CSV')
    src = ROOT / p
    if src.exists() and src.is_file() and src.stat().st_size > LARGE_FILE_LIMIT:
        flags.append(f'larger than {LARGE_FILE_LIMIT // (1024*1024)} MiB')
    return flags


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--tracked', action='store_true', help='inspect all tracked paths')
    group.add_argument('--staged', action='store_true', help='inspect staged additions (default)')
    opts = parser.parse_args()
    mode = 'tracked' if opts.tracked else 'staged'
    try:
        paths = git_paths(mode)
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f'Git inspection failed: {e}', file=sys.stderr)
        return 2
    offenders = [(p, flags) for p in paths if (flags := check(p))]
    if offenders:
        print(f'BLOCKED: {len(offenders)} suspicious {mode} path(s):', file=sys.stderr)
        for p, flags in offenders:
            print(f'  {p}: {", ".join(flags)}', file=sys.stderr)
        print('Remove from index with: git rm --cached -- <path>', file=sys.stderr)
        return 1
    print(f'OK: {len(paths)} {mode} paths; no large datasets or generated CSVs detected.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
