#!/usr/bin/env python3
"""Stream ASCII/binary PLY points to x,y,z CSV accepted by ventus-cli.

Usage:
  python3 ply_to_ventus_xyz.py plot.ply --list-instances
  python3 ply_to_ventus_xyz.py plot.ply -o all.csv
  python3 ply_to_ventus_xyz.py plot.ply -o target.csv --instance 42 --context-output neighbours.csv

No third-party modules required. Does not reproject, recenter, or interpolate points.
"""
from __future__ import annotations

import argparse
from collections import Counter
import math
from pathlib import Path
import struct
import sys
from dataclasses import dataclass, field
from typing import BinaryIO, Iterator, Any

# PLY standard scalar type names (plus common aliases).
SCALAR_TYPES = {
    'char': 'b', 'int8': 'b',
    'uchar': 'B', 'uint8': 'B',
    'short': 'h', 'int16': 'h',
    'ushort': 'H', 'uint16': 'H',
    'int': 'i', 'int32': 'i',
    'uint': 'I', 'uint32': 'I',
    'float': 'f', 'float32': 'f',
    'double': 'd', 'float64': 'd',
    'longlong': 'q', 'int64': 'q',
    'ulonglong': 'Q', 'uint64': 'Q',
}

@dataclass
class Property:
    name: str
    data_type: str
    count_type: str | None = None

    @property
    def is_list(self) -> bool:
        return self.count_type is not None

@dataclass
class Element:
    name: str
    count: int
    properties: list[Property] = field(default_factory=list)


def read_header(inp: BinaryIO) -> tuple[str, list[Element]]:
    if inp.readline().strip() != b'ply':
        raise ValueError('Not a PLY file: missing ply signature')
    fmt = None
    elements: list[Element] = []
    for _ in range(200000):
        raw = inp.readline()
        if not raw:
            raise ValueError('Truncated PLY header: no end_header')
        line = raw.decode('ascii', errors='strict').strip()
        if line == 'end_header':
            break
        if not line or line.startswith(('comment ', 'obj_info ')):
            continue
        parts = line.split()
        if parts[0] == 'format':
            if len(parts) < 3 or parts[1] not in ('ascii', 'binary_little_endian', 'binary_big_endian'):
                raise ValueError('Unsupported PLY format: ' + line)
            fmt = parts[1]
        elif parts[0] == 'element':
            if len(parts) != 3:
                raise ValueError('Malformed PLY element: ' + line)
            count = int(parts[2])
            if count < 0:
                raise ValueError('Negative element count')
            elements.append(Element(parts[1], count))
        elif parts[0] == 'property':
            if not elements:
                raise ValueError('PLY property before any element')
            if len(parts) == 3:
                dtype, name = parts[1], parts[2]
                if dtype not in SCALAR_TYPES:
                    raise ValueError('Unknown PLY scalar type: ' + dtype)
                elements[-1].properties.append(Property(name, dtype))
            elif len(parts) == 5 and parts[1] == 'list':
                count_type, dtype, name = parts[2], parts[3], parts[4]
                if count_type not in SCALAR_TYPES or dtype not in SCALAR_TYPES:
                    raise ValueError('Unknown PLY list type: ' + line)
                elements[-1].properties.append(Property(name, dtype, count_type))
            else:
                raise ValueError('Malformed PLY property: ' + line)
    else:
        raise ValueError('PLY header too long')
    if fmt is None:
        raise ValueError('Missing PLY format')
    if sum(e.name == 'vertex' for e in elements) != 1:
        raise ValueError('Expected exactly one vertex element')
    return fmt, elements


def records(inp: BinaryIO, fmt: str, elements: list[Element]) -> Iterator[tuple[int, dict[str, Any]]]:
    """Read each vertex record, supporting other elements before it and lists."""
    binary = fmt != 'ascii'
    endian = '<' if fmt == 'binary_little_endian' else '>'
    for element in elements:
        props = element.properties
        if binary:
            # Fast, fixed-size binary records (normal for forest-plot vertices).
            if all(not p.is_list for p in props):
                layout = struct.Struct(endian + ''.join(SCALAR_TYPES[p.data_type] for p in props))
                if element.name != 'vertex':
                    inp.seek(element.count * layout.size, 1)
                    continue
                names = [p.name for p in props]
                for i in range(element.count):
                    raw = inp.read(layout.size)
                    if len(raw) != layout.size:
                        raise ValueError(f'Truncated {element.name} record {i}')
                    yield i, dict(zip(names, layout.unpack(raw)))
            else:
                # Flexible fallback for list-valued properties.
                for i in range(element.count):
                    vals: dict[str, Any] = {}
                    for prop in props:
                        if prop.is_list:
                            count_reader = struct.Struct(endian + SCALAR_TYPES[prop.count_type])
                            raw = inp.read(count_reader.size)
                            if len(raw) != count_reader.size:
                                raise ValueError('Truncated binary PLY list count')
                            n = count_reader.unpack(raw)[0]
                            if n < 0:
                                raise ValueError('Negative PLY list count')
                            elem_reader = struct.Struct(endian + SCALAR_TYPES[prop.data_type])
                            payload = inp.read(n * elem_reader.size)
                            if len(payload) != n * elem_reader.size:
                                raise ValueError('Truncated binary PLY list')
                            if element.name == 'vertex':
                                vals[prop.name] = None
                        else:
                            reader = struct.Struct(endian + SCALAR_TYPES[prop.data_type])
                            raw = inp.read(reader.size)
                            if len(raw) != reader.size:
                                raise ValueError('Truncated binary PLY value')
                            if element.name == 'vertex':
                                vals[prop.name] = reader.unpack(raw)[0]
                    if element.name == 'vertex':
                        yield i, vals
        else:
            for i in range(element.count):
                raw = inp.readline()
                if not raw:
                    raise ValueError(f'Truncated {element.name} record {i}')
                if element.name != 'vertex':
                    continue
                pieces = raw.decode('ascii').split()
                vals: dict[str, Any] = {}
                pos = 0
                for prop in props:
                    if prop.is_list:
                        if pos >= len(pieces):
                            raise ValueError('Missing ASCII PLY list count')
                        n = int(pieces[pos]); pos += 1
                        if n < 0 or pos+n > len(pieces):
                            raise ValueError('Invalid ASCII PLY list length')
                        pos += n
                        vals[prop.name] = None
                    else:
                        if pos >= len(pieces):
                            raise ValueError('Missing ASCII PLY scalar')
                        value = pieces[pos]; pos += 1
                        code = SCALAR_TYPES[prop.data_type]
                        vals[prop.name] = float(value) if code in ('f','d') else int(value)
                yield i, vals
        if element.name == 'vertex':
            return


def instance_id(value: Any) -> int:
    v = float(value) if isinstance(value, str) else value
    iv = int(v)
    if not math.isfinite(float(v)) or iv != v:
        raise ValueError('Nonintegral instance label in PLY: ' + str(value))
    return iv


def main() -> int:
    p = argparse.ArgumentParser(description='Convert ASCII or binary PLY to VENTUS x,y,z CSV, optionally splitting one tree by instance ID.')
    p.add_argument('input', type=Path, help='Input .ply forest point cloud')
    p.add_argument('-o', '--output', type=Path, help='XYZ CSV output (or selected tree when --instance is used)')
    p.add_argument('--instance', type=int, help='Select a target-tree instance ID from the PLY vertex property')
    p.add_argument('--instance-field', default='instance', help='PLY vertex property holding tree instance ID (default: instance)')
    p.add_argument('--context-output', type=Path, help='Output every other point to this neighbours CSV (requires --instance)')
    p.add_argument('--list-instances', action='store_true', help='Print counts by tree instance ID; do not produce CSV')
    p.add_argument('--stride', type=int, default=1, help='Write every Nth eligible point in each output (quick, non-spatial downsampling)')
    p.add_argument('--no-header', action='store_true', help='Omit x,y,z header')
    a = p.parse_args()
    if a.stride < 1:
        p.error('--stride must be >= 1')
    if a.context_output and a.instance is None:
        p.error('--context-output requires --instance')
    if not a.list_instances and not a.output:
        p.error('Use -o OUTPUT or --list-instances')
    if a.output and a.context_output and a.output.resolve() == a.context_output.resolve():
        p.error('Target and context outputs must differ')
    for destination in (a.output, a.context_output):
        if destination and destination.resolve() == a.input.resolve():
            p.error('Output must not overwrite the input PLY')

    with a.input.open('rb') as inp:
        fmt, elements = read_header(inp)
        vp = next(e for e in elements if e.name == 'vertex')
        names = {prop.name for prop in vp.properties}
        if not {'x','y','z'} <= names:
            raise ValueError('PLY vertex element must contain x, y, z properties')
        if (a.instance is not None or a.list_instances) and a.instance_field not in names:
            raise ValueError(f"PLY has no '{a.instance_field}' vertex property; found: {', '.join(sorted(names))}")

        outputs = []
        try:
            if not a.list_instances:
                for path in (a.output, a.context_output):
                    if path is not None:
                        path.parent.mkdir(parents=True, exist_ok=True)
                        out = path.open('w', encoding='utf-8', newline='')
                        if not a.no_header:
                            out.write('x,y,z\n')
                        outputs.append(out)
                    else:
                        outputs.append(None)
            else:
                outputs = [None, None]

            total = eligible = skipped_nonfinite = written_target = written_context = 0
            seen_target = seen_context = 0
            counts: Counter[int] = Counter()
            for _, v in records(inp, fmt, elements):
                total += 1
                inst = None
                if a.list_instances or a.instance is not None:
                    inst = instance_id(v[a.instance_field])
                if a.list_instances:
                    counts[inst] += 1
                    continue
                x, y, z = float(v['x']), float(v['y']), float(v['z'])
                if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
                    skipped_nonfinite += 1
                    continue
                eligible += 1
                is_target = a.instance is None or inst == a.instance
                if is_target:
                    seen_target += 1
                    if (seen_target-1) % a.stride == 0:
                        outputs[0].write(f'{x:.9f},{y:.9f},{z:.9f}\n')
                        written_target += 1
                elif outputs[1] is not None:
                    seen_context += 1
                    if (seen_context-1) % a.stride == 0:
                        outputs[1].write(f'{x:.9f},{y:.9f},{z:.9f}\n')
                        written_context += 1
            if a.list_instances:
                print(f'PLY: {a.input} | {fmt} | {total:,} vertices | {len(counts):,} instance IDs')
                for instance, count in counts.most_common():
                    print(f'{instance}\t{count}')
            else:
                print(f'PLY: {a.input} | {fmt} | {total:,} vertices')
                print(f'XYZ output: {a.output} ({written_target:,} points)')
                if a.context_output:
                    print(f'Context output: {a.context_output} ({written_context:,} points)')
                print(f'Skipped nonfinite coordinates: {skipped_nonfinite:,}; stride: {a.stride}')
                if a.instance is not None and written_target == 0:
                    print(f'WARNING: no vertices found with {a.instance_field}={a.instance}', file=sys.stderr)
                    return 1
        finally:
            for f in outputs:
                if f is not None:
                    f.close()
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, struct.error) as ex:
        print(f'ERROR: {ex}', file=sys.stderr)
        sys.exit(2)
