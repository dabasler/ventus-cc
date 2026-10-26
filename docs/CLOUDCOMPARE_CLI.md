# qVENTUS native CloudCompare command line (0.2.0-dev)

**Status:** Source implemented; standalone calculation tests pass. **The plugin binary needs compiling and verifying on your CloudCompare 2.14 checkout** (`~/dev/CloudCompare`). The integration test below requires that local installation.

The plugin registers the `-VENTUS` command in CloudCompare through `qVENTUS::registerCommands`. It uses **the same VENTUS engine, per-point scalar fields and persistent crown metadata as the GUI**. It can read `.bin` via CloudCompare's `-O` command, including loaded segmented forest clouds. No PLY→CSV conversion is required.

## Build in the existing development tree

```bash
cd ~/dev/ventus-cc
python3 tools/link_into_cloudcompare.py ~/dev/CloudCompare
cd ~/dev/CloudCompare
cmake -S . -B build-dev -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/dev/CloudCompare-install-dev" \
  -DPLUGIN_STANDARD_QVENTUS=ON \
  -DPLUGIN_STANDARD_QSOLIS=ON
cmake --build build-dev --target QVENTUS_PLUGIN --parallel
cmake --install build-dev --prefix ~/dev/CloudCompare-install-dev
```

Use **the installed CloudCompare executable** to locate the plugin.

## Discover the tree names in a native BIN

```bash
CC=~/dev/CloudCompare-install-dev/bin/CloudCompare
"$CC" -SILENT -AUTO_SAVE OFF -O /data/wytham.bin -VENTUS -LIST
```

The `-LIST` command enumerates **each loaded cloud/mesh** and its zero-based `-TARGET_INDEX`. Entity names are matched **exactly and case sensitively**. If two entities have the same name, select one with `-TARGET_INDEX i`.

## Calculate one tree with all neighbouring trees

```bash
mkdir -p ~/dev/ventus-cc/tests/results/wytham-bin
"$CC" -SILENT -AUTO_SAVE OFF \
  -O /data/wytham.bin \
  -VENTUS \
  -TARGET "Tree_42" \
  -CONTEXT ALL \
  -DIRECTION 270 -SPEED 5 \
  -VOXEL 0.5 -RANGE 25 \
  -ATTENUATION 0.15 \
  -AZIMUTH_SAMPLES 16 -ELEVATION_SAMPLES 3 \
  -COMPARE_NEIGHBOURS -COMPASS \
  -EXPORT_CSV ~/dev/ventus-cc/tests/results/wytham-bin/tree42-crown.csv \
  -EXPORT_BIN ~/dev/ventus-cc/tests/results/wytham-bin/tree42-VENTUS.bin
```

The `-EXPORT_CSV` command writes the requested crown summary CSV and a companion `*-profiles.csv`. `-EXPORT_BIN` saves **only the analyzed target entity**, with VENTUS scalar fields and crown metadata preserved. `-EXPORT_POINTS file.csv` optionally writes an XYZ+metrics CSV, potentially very large for full TLS scans.

`-OVERWRITE` is required if an output path already exists. Otherwise, the command errors without overwriting previous results.

### Target-only, all loaded, and named neighbour selection

- `-CONTEXT TARGET` (default): self-occlusion only.
- `-CONTEXT ALL`: target plus all other loaded clouds/meshes.
- `-CONTEXT NAMED -CONTEXT_NAMES "Tree_12,Tree_13"`: target plus exactly the named neighbours. Names containing commas are not supported by this option.

All loaded context is used even if a neighbouring cloud was hidden in the GUI when the BIN was saved: the CLI has no GUI visibility filter.

### Options

| Command option | Meaning | Default |
|---|---|---|
| `-TARGET "entity name"` | Select target cloud/mesh by exact name | Required if multiple entities |
| `-TARGET_INDEX i` | Select by index reported by `-LIST` | — |
| `-CONTEXT TARGET\|ALL\|NAMED` | Occlusion geometry | TARGET |
| `-CONTEXT_NAMES "name1,name2"` | Name list for NAMED context | — |
| `-DIRECTION deg` | Wind coming **from** 0°=N, 90°=E | 270 |
| `-SPEED m/s` | Reference wind speed | 5 |
| `-ROSE file.csv` | Wind rose instead of direction/speed | — |
| `-VOXEL m` | Occupancy voxel resolution | 0.5 |
| `-RANGE m` | Maximum geometric ray range | 50 |
| `-ATTENUATION 1/m` | Empirical exponential attenuation | 0.15 |
| `-AZIMUTH_SAMPLES n` | Level 1 sampling | 16 |
| `-ELEVATION_SAMPLES n` | Upwind fan samples | 3 |
| `-MIN_ELEVATION deg` | Fan minimum elevation | 0 |
| `-MAX_ELEVATION deg` | Fan maximum elevation | 30 |
| `-SECTOR_HALF_WIDTH deg` | Half-width of directional fan | 15 |
| `-EMPIRICAL ON\|OFF` | Write Level 2 scalar fields | ON |
| `-COMPARE_NEIGHBOURS` | Add target-only reference and neighbour difference | Off |
| `-COMPASS` | Write 8 directional scalar fields | Off |
| `-EXPORT_CSV path.csv` | Crown summary and profiles | None |
| `-EXPORT_POINTS path.csv` | XYZ and all point metrics | None |
| `-EXPORT_BIN path.bin` | Native CloudCompare BIN target with results | None |
| `-OVERWRITE` | Allow replacing existing output files | Off |
| `-LIST` | List loaded entities, no calculation | — |
| `-HELP` | Print CLI options | — |

### Run a numeric sensitivity/regression sweep

```bash
python3 ~/dev/ventus-cc/tools/run_ventus_bin_regression.py \
  --cc ~/dev/CloudCompare-install-dev/bin/CloudCompare \
  --bin /data/wytham.bin \
  --target "Tree_42" \
  --context ALL \
  --direction 270 --speed 5 --voxel 0.5 --range 25 \
  --out ~/dev/ventus-cc/tests/results/wytham-bin \
  --sweep
```

The runner checks BIN outputs, CSV outputs, invariance of CWEI with attenuation, equality of crown pressure ratio to 1 at `k=0`, and monotonic decline as `k` increases. It runs against your installed CloudCompare and uses a **fresh load of the same BIN for each test**.

## Important assumptions and limitations

- Every separate tree **must be a separate cloud or mesh** in the BIN for `-TARGET "Tree_42"` to work. A single PLY/CloudCompare cloud with an `instance` scalar field but no separate entities is not directly selectable by instance ID in this version.
- CloudCompare may store different shifts/scales for different entities. The CLI normalizes neighbouring geometry into the target's local frame, but currently **requires a target global scale of 1.0**, and expects the coordinate units to be metres.
- `-EXPORT_BIN` saves the target only. For an entire scene, use CloudCompare's general `-C_EXPORT_FMT BIN -SAVE_CLOUDS ALL_AT_ONCE` workflow after `-VENTUS`, noting that the standard cloud saver may not preserve the full hierarchy.
- BIN output is CloudCompare-native. The standalone `ventus-cli` remains XYZ/CSV-only; it has not gained a BIN reader.
- VENTUS Level 1 is a voxelized geometric accessibility model; Level 2 is uncalibrated exponential attenuation, **not a validated flow solver or windthrow model**.
- For full forests the plugin uses a voxel grid and performs ray traces for every target point, potentially demanding substantial memory and CPU. Start with one downsampled tree and a limited forest context.
- Exactly the loaded entities are considered. Truncated Wytham plot boundaries may bias geometric exposure upward. No boundary confidence metric is implemented yet.
- Qt/CloudCompare-specific compilation and BIN roundtrips have **not been run in this environment**; the Python integration runner is provided for verification on your local machine.
