#!/usr/bin/env python3
"""Read-only inventory and triage of a forest dataset for qVENTUS.

No dependencies beyond Python 3.8+. Headers are inspected without loading
point arrays. Optionally calls an installed CloudCompare CLI for BIN entity lists.
"""
import argparse
import collections
import csv
import json
import os
import re
import shlex
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

DATA_EXT = {'.ply', '.bin', '.las', '.laz', '.e57', '.xyz', '.csv', '.asc', '.txt',
            '.pcd', '.sbf', '.ptx', '.pts', '.obj', '.stl', '.off', '.zip', '.7z',
            '.rar', '.gz', '.parquet', '.feather', '.npz', '.npy'}
TREE_ID_NAMES = {'instance', 'instance_id', 'instanceid', 'tree_id', 'treeid',
                 'tree', 'segment_id', 'segmentid', 'label', 'object_id'}
GEOMETRY_EXT = {'.ply', '.bin', '.las', '.laz', '.e57', '.xyz', '.csv', '.asc',
                '.txt', '.pcd', '.sbf', '.ptx', '.pts', '.obj', '.stl', '.off'}


def relpath(path, root):
    return str(path.relative_to(root))


def classify(path):
    ext = path.suffix.lower()
    return ext[1:].upper() if ext else '(none)'


def detect_tree_field(names):
    for name in names:
        normalized = re.sub(r'[^a-z0-9_]', '', name.casefold())
        if normalized in TREE_ID_NAMES:
            return name
    return None


def ply_header(path, max_bytes=1024 * 1024):
    # PLY headers are ASCII even when the vertices are stored in binary.
    with path.open('rb') as stream:
        if stream.readline().strip() != b'ply':
            raise ValueError('Not a valid PLY header')
        total = 4
        element = ''
        properties = []
        vertex_count = None
        encoding = None
        for _ in range(50000):
            line = stream.readline()
            total += len(line)
            if not line or total > max_bytes:
                raise ValueError('PLY header missing end_header or too long')
            text = line.decode('ascii', errors='replace').strip()
            parts = text.split()
            if not parts:
                continue
            if parts[0] == 'format' and len(parts) > 1:
                encoding = parts[1]
            elif parts[0] == 'element' and len(parts) >= 3:
                element = parts[1]
                if element == 'vertex':
                    vertex_count = int(parts[2])
            elif parts[0] == 'property' and element == 'vertex' and len(parts) >= 3:
                # The last token is the property name (handles list properties).
                properties.append(parts[-1])
            elif parts[0] == 'end_header':
                return {'points': vertex_count, 'encoding': encoding,
                        'fields': properties, 'tree_field': detect_tree_field(properties)}
    raise ValueError('PLY header incomplete')


def las_header(path):
    # LAS and LAZ share the same uncompressed LAS header; no LAS dependency needed.
    with path.open('rb') as stream:
        b = stream.read(375)
    if len(b) < 227 or b[:4] != b'LASF':
        raise ValueError('Not a LAS/LAZ header')
    major, minor = b[24], b[25]
    points = struct.unpack_from('<I', b, 107)[0]
    if major == 1 and minor >= 4 and len(b) >= 255:
        extended = struct.unpack_from('<Q', b, 247)[0]
        if extended:
            points = extended
    return {'points': points, 'encoding': 'LAS %s.%s (metadata only)' % (major, minor),
            'fields': [], 'tree_field': None}


def delimited_header(path):
    # Sample only a few KB; coordinates and labels may not exist in first row.
    with path.open('rb') as stream:
        raw = stream.read(8192)
    text = raw.decode('utf-8-sig', errors='replace')
    first = next((s.strip() for s in text.splitlines() if s.strip()), '')
    if not first:
        return {'fields': [], 'tree_field': None, 'encoding': 'empty file'}
    try:
        delimiter = max([',', ';', '\t'], key=lambda d: first.count(d))
        fields = next(csv.reader([first], delimiter=delimiter)) if delimiter in first else first.split()
    except csv.Error:
        fields = first.split()
    fields = [s.strip().strip('"') for s in fields]
    # Only treat textual first rows as column headers.
    if fields and all(re.fullmatch(r'[+\-]?\d+(?:\.\d+)?(?:[Ee][+\-]?\d+)?', s) for s in fields):
        return {'fields': [], 'tree_field': None, 'encoding': 'numeric data; no column names'}
    return {'fields': fields, 'tree_field': detect_tree_field(fields),
            'encoding': ('delimited header: ' + repr(delimiter))}


def scan_files(root):
    results = []
    skipped = 0
    for folder, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if not os.path.islink(os.path.join(folder, d)))
        for name in sorted(files):
            path = Path(folder) / name
            try:
                stat = path.stat()
            except OSError as ex:
                results.append({'path': str(path), 'relative_path': relpath(path, root),
                                'type': classify(path), 'bytes': 0,
                                'error': 'Cannot stat: ' + str(ex)})
                continue
            item = {'path': str(path), 'relative_path': relpath(path, root),
                    'type': classify(path), 'bytes': stat.st_size,
                    'points': None, 'encoding': '', 'fields': [],
                    'tree_field': None, 'bin_entities': [], 'error': ''}
            try:
                ext = path.suffix.lower()
                if ext == '.ply':
                    item.update(ply_header(path))
                elif ext in {'.las', '.laz'}:
                    item.update(las_header(path))
                elif ext in {'.csv', '.xyz', '.asc', '.txt'}:
                    item.update(delimited_header(path))
            except (OSError, ValueError, UnicodeError) as ex:
                item['error'] = str(ex)
            results.append(item)
            if path.suffix.lower() not in DATA_EXT:
                skipped += 1
    return results, skipped


def inspect_bins(items, cc, limit, timeout):
    binary_files = [it for it in items if it['type'] == 'BIN']
    if limit > 0:
        binary_files = binary_files[:limit]
    for idx, item in enumerate(binary_files, 1):
        cmd = [str(cc), '-SILENT', '-AUTO_SAVE', 'OFF', '-O', item['path'], '-VENTUS', '-LIST']
        print('Inspecting BIN %d/%d: %s' % (idx, len(binary_files), item['relative_path']), file=sys.stderr)
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors='replace', timeout=timeout, check=False)
            output = proc.stdout
            item['bin_exit_code'] = proc.returncode
            # VENTUS versions print "[VENTUS] entity 0: <name> (N points)".
            found = re.findall(r'\[VENTUS\]\s+entity\s+(\d+):\s*(.+?)\s*\((\d+)\s+points\)', output)
            item['bin_entities'] = [{'index': int(i), 'name': nm, 'points': int(n)} for i, nm, n in found]
            if not found:
                item['bin_entities'] = [
                    {'index': None, 'name': 'unlabelled_cloud', 'points': int(n)}
                    for n in re.findall(r'Found one cloud with (\d+) points', output)]
            if proc.returncode or 'Unknown or misplaced command' in output or not item['bin_entities']:
                item['error'] = ('BIN inspection exit %s; '%proc.returncode +
                                 ' | '.join(output.splitlines()[-6:]))[:750]
        except (OSError, subprocess.TimeoutExpired) as ex:
            item['error'] = 'BIN inspection failed: ' + str(ex)


def human_size(n):
    for unit in ('B', 'KiB', 'MiB', 'GiB', 'TiB'):
        if n < 1024 or unit == 'TiB':
            return ('%d B' % n) if unit == 'B' else ('%.2f %s' % (n, unit))
        n /= 1024.0


def plausible_tree_file(item):
    p = item['relative_path'].lower()
    if item['type'] not in {'PLY', 'LAS', 'LAZ', 'BIN', 'CSV', 'XYZ', 'ASC', 'TXT'}:
        return False
    return (bool(re.search(r'(?:^|[/_-])tree[_-]?\d+|(?:^|/)individual[_-]?trees?/', p))
            or bool(re.search(r'(?:^|/)trees?[/\\]', p)))


def choices(items):
    labelled = [i for i in items if i.get('tree_field') and i['type'] in {'PLY', 'CSV', 'XYZ', 'ASC', 'TXT'}]
    labelled.sort(key=lambda i: (-(i.get('points') or 0), -i['bytes']))
    trees = [i for i in items if plausible_tree_file(i)]
    bins = [i for i in items if i['type'] == 'BIN']
    bins.sort(key=lambda i: -i['bytes'])
    return labelled, trees, bins


def report(root, items, skipped, inspect_bin, cc):
    counts = collections.Counter(it['type'] for it in items)
    bytes_total = sum(it['bytes'] for it in items)
    labelled, trees, bins = choices(items)
    lines = [
        'VENTUS / Wytham Woods local dataset audit',
        '=' * 45,
        'Dataset root: ' + str(root),
        'Generated: ' + datetime.now(timezone.utc).isoformat(),
        'Read-only inspection: headers, names, sizes; never loads PLY points',
        '',
        'FILES / STORAGE',
        'Total files: %d (%s)' % (len(items), human_size(bytes_total)),
        'Other/non-point-cloud files: %d' % skipped,
        'By extension: ' + ', '.join('%s=%d' % (x, n) for x, n in counts.most_common()),
        '',
        'POTENTIAL SEGMENTATION SOURCES',
    ]
    if labelled:
        lines.append('Files with an explicit instance/tree identifier in a header (top 12):')
        for it in labelled[:12]:
            lines.append('  %s | %s | field=%s | points=%s' %
                         (it['relative_path'], human_size(it['bytes']), it['tree_field'], it.get('points', '?')))
    else:
        lines.append('No directly detectable instance/tree-ID field found in scanned PLY/CSV headers.')
    lines.extend(['', 'POSSIBLE INDIVIDUAL TREE FILES'])
    if trees:
        lines.append('Filename/directory heuristic (not proof of actual segmentation): %d candidates' % len(trees))
        for it in trees[:12]:
            lines.append('  ' + it['relative_path'])
    else:
        lines.append('No filenames clearly suggest separate tree files.')
    lines.extend(['', 'BIN FILES / CLOUDCOMPARE ENTITIES'])
    if bins:
        for it in bins[:30]:
            lines.append('  %s (%s)' % (it['relative_path'], human_size(it['bytes'])))
            if it.get('bin_entities'):
                for e in it['bin_entities'][:30]:
                    lines.append('    entity %s: %s — %s pts' % (e['index'], e['name'], e['points']))
            elif not inspect_bin:
                lines.append('    entity structure unknown; use --inspect-bin --cc /path/to/CloudCompare')
            if it['error']:
                lines.append('    WARNING: ' + it['error'])
        if len(bins) > 30:
            lines.append('  ... additional BINs are listed in files.csv')
    else:
        lines.append('No BIN files detected.')
    lines.extend(['', 'LARGEST GEOMETRY / ARCHIVE FILES'])
    for it in sorted((i for i in items if Path(i['path']).suffix.lower() in DATA_EXT),
                     key=lambda x: -x['bytes'])[:15]:
        lines.append('  %s | %s | %s%s' % (it['relative_path'], human_size(it['bytes']), it['type'],
                                         (' | points='+str(it['points'])) if it.get('points') is not None else ''))
    errors = [i for i in items if i.get('error')]
    if errors:
        lines.extend(['', 'WARNINGS (%d)' % len(errors)])
        for it in errors[:15]:
            lines.append('  %s: %s' % (it['relative_path'], it['error']))
    lines.extend(['', 'RECOMMENDED NEXT STEP', '-'*24])
    if labelled:
        p = labelled[0]
        lines += [
            '1. Best candidate: segmented/labelled file detected:',
            '   ' + p['path'],
            '   Property: ' + p['tree_field'],
            '2. Inspect instance IDs, then choose an interior tree. For PLY:',
        ]
        if p['type'] == 'PLY':
            lines.append('   python3 ~/dev/ventus-cc/tools/ply_to_ventus_xyz.py ' +
                         shlex.quote(p['path']) + ' --list-instances')
            lines.append('3. Extract chosen ID and remaining geometry into aligned CSV files:')
            lines.append('   python3 ~/dev/ventus-cc/tools/ply_to_ventus_xyz.py ' +
                         shlex.quote(p['path']) +
                         ' --instance <TREE_ID> -o target.csv --context-output neighbours.csv')
        else:
            lines.append('   Inspect the field values; convert/group by tree ID before VENTUS.')
    elif trees:
        lines += [
            '1. Possible already separated trees detected. Check a few filenames:',
            '   ' + trees[0]['path'],
            '2. Pick one central tree, preserve a surrounding plot as context.',
            '3. Use individual trees as CloudCompare entities or convert to CSV for ventus-cli.',
        ]
    elif bins:
        b = bins[0]
        lines += [
            '1. Existing BIN candidate: ' + b['path'],
            '2. Check CloudCompare entity structure and scalar fields:',
        ]
        if not inspect_bin:
            lines.append('   python3 scan_wytham_for_ventus.py ' + shlex.quote(str(root)) +
                         ' --inspect-bin --cc ' + shlex.quote(str(cc or '~/dev/CloudCompare-install-dev/bin/CloudCompare')))
        lines += [
            '3. If the BIN contains ONE entire forest cloud, -TARGET 0 selects the',
            '   whole forest. Existing qVENTUS CLI cannot select a tree by scalar-field ID.',
            '   Inspect the GUI scalar-field list for instance/treeID or locate an',
            '   upstream labelled PLY before choosing a single-tree test.',
        ]
    else:
        lines += [
            'No obvious segmented point-cloud source found. Inspect compressed',
            'archives and metadata files, or provide a directory listing to identify',
            'where the segment labels are stored.',
        ]
    lines += [
        '',
        'AFTER TARGET/CONTEXT PREPARATION',
        'Run small target-only and neighbour-context tests first, then sweep',
        'attenuation k={0,0.15,0.5,1} and voxel sizes {0.25,0.5,1} m.',
        'Do not assume "one cloud" means "one tree", or that PLY has labels',
        'just because an archive is named "segmented".',
        'NOTE: BIN listing does not enumerate CloudCompare scalar-field names.',
    ]
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root', nargs='?', default='/home/db/dev/CloudCompare_TestData/wytham',
                    help='Wytham dataset directory')
    ap.add_argument('--out', default='./wytham-ventus-inventory',
                    help='Output directory for report.txt, files.csv, summary.json')
    ap.add_argument('--inspect-bin', action='store_true',
                    help='Ask installed CloudCompare to list entities in BINs (loads full clouds; opt-in)')
    ap.add_argument('--cc', type=Path, help='CloudCompare executable path used with --inspect-bin')
    ap.add_argument('--max-bin', type=int, default=10,
                    help='Maximum BIN files to open in CloudCompare (default 10; 0 = all)')
    ap.add_argument('--timeout', type=int, default=180,
                    help='Timeout in seconds per BIN inspection (default 180)')
    args = ap.parse_args()
    root = Path(args.root).expanduser().resolve()
    if not root.is_dir():
        ap.error('Dataset directory not found: ' + str(root))
    cc = args.cc.expanduser().resolve() if args.cc else None
    if args.inspect_bin and (not cc or not cc.is_file()):
        ap.error('--inspect-bin requires --cc /path/to/CloudCompare executable')
    out = Path(args.out).expanduser().resolve()
    # Don't include previous reports if user places --out inside root.
    items, skipped = scan_files(root)
    if args.inspect_bin:
        inspect_bins(items, cc, args.max_bin, args.timeout)
    out.mkdir(parents=True, exist_ok=True)
    columns = ['relative_path','type','bytes','points','encoding','tree_field','fields','bin_entities','error']
    with (out / 'files.csv').open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for it in items:
            writer.writerow({k: json.dumps(it.get(k)) if k in ('fields','bin_entities') else it.get(k)
                             for k in columns})
    counts = collections.Counter(it['type'] for it in items)
    labelled, trees, bins = choices(items)
    summary = {
        'root': str(root), 'total_files': len(items),
        'total_bytes': sum(it['bytes'] for it in items),
        'by_type': dict(counts),
        'labelled_candidates': [i['relative_path'] for i in labelled],
        'separate_tree_filename_candidates': [i['relative_path'] for i in trees],
        'bin_files': [{'file': i['relative_path'], 'entities': i['bin_entities'],
                       'error': i['error']} for i in bins],
        'issues': [{'file': i['relative_path'], 'error': i['error']} for i in items if i.get('error')],
    }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    text = report(root, items, skipped, args.inspect_bin, cc)
    (out / 'report.txt').write_text(text, encoding='utf-8')
    print(text)
    print('Saved: ' + str(out / 'report.txt'))
    print('       ' + str(out / 'files.csv'))
    print('       ' + str(out / 'summary.json'))


if __name__ == '__main__':
    main()
