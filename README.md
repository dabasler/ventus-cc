# VENTUS — 3D Wind Exposure and Shelter Analysis

**Repository:** `ventus-cc` · **Plugin identifier:** `qVENTUS` · **CloudCompare menu:** `VENTUS` · **Version:** `0.2.0-dev` (native CloudCompare BIN CLI preview)

VENTUS is a CloudCompare Standard plugin for analysing directional geometric wind exposure and *empirical* wind attenuation on point clouds and indexed triangular meshes. It was designed as a wind-oriented companion to [qSOLIS](https://github.com/dabasler/solis-cc), using the same broad concepts of scalar-field outputs, occlusion context, and crown statistics. This repository is an original implementation, **not a copy of qSOLIS source code**.

**Status:** The independent C++17 engine and CSV command-line program compile and pass included unit tests. The CloudCompare 2.14+/Qt 6 plugin adapter is supplied as buildable source, but has **not been compiled or tested against a full CloudCompare installation** in the current development environment. Do not treat this as a validated binary release.

## What is implemented

### Level 1 — Geometric exposure

Per point/mesh vertex, stored as CloudCompare scalar fields:

| Field | Units | Definition |
|---|---|---|
| `VENTUS WEF` | 0–1 | Mean unobstructed ray fraction over equally spaced compass azimuths and sampled elevation angles |
| `VENTUS WSF` | 0–1 | `1 - WEF` |
| `VENTUS DWE` | 0–1 | Open ray fraction toward the wind rose's **most frequent** direction (or single direction) |
| `VENTUS WEI` | 0–1 | Frequency-weighted directional ray openness |
| `VENTUS WPWE` | 0–1 | Frequency × reference speed² weighted directional ray openness |
| `VENTUS Wind Exposure Frequency` | 0–1 | Frequency share of wind bins for which sampled openness ≥ configured threshold |
| `VENTUS Upwind Openness` | 0–1 | Same as DWE, included for direct interpretation |
| `VENTUS Shelter Angle` | degrees | Highest *sampled* blocked nonnegative elevation in the dominant sector (not an exact Sx angle) |
| `VENTUS Obstruction Distance` | m | Average first-hit distance on the lowest-elevation rays in the dominant sector; **right-censored** at maximum range |
| `VENTUS Upwind Occupied Path` | m | Mean occupied-voxel chord length along rays in the dominant wind sector |
| `VENTUS Upwind Path Occupancy` | 0–1 | Occupied-path length divided by maximum ray range |
| `VENTUS Exposure 0deg...315deg` | 0–1 | Optional 8-direction compass scalar fields |
| `VENTUS Neighbour Shelter Delta WEI` | difference | Optional `target-only WEI − context WEI`, requiring an additional calculation |

At entity/crown level (persistent JSON metadata and CSV exports): **CWEI, pressure-weighted crown exposure, WEF mean, sheltered fraction, weighted P10/P50/P90, exposed crown fraction, upper/lower crown exposure, crown directional asymmetry, dominant exposure direction, eight-direction exposure, windward projected crown area for eight directions, maximum and wind-rose-weighted projected area, crown load potential (area × speed²), mean occupied upwind path length, and vertical exposure profiles**.

Point clouds are aggregated with **equal occupied-voxel weight**; indexed meshes with **triangle-area-based vertex weights**. Crown quantiles use these weights as well.

### Level 2 — Empirical wind attenuation (optional)

VENTUS integrates occupied path length `L` along each sampled upwind ray and estimates relative speed transmissivity as

```text
T = exp(-k * L)
```

Here `k` is an adjustable empirical attenuation coefficient in **1/m**. Voxel occupancy is a geometric proxy for obstruction; this equation is **not a universal or calibrated law for tree canopies**. Averaged ray transmissivity is combined with the input wind rose to estimate:

- `VENTUS Empirical Speed Ratio` (0–1), weighted by reference speed and occurrence frequency;
- `VENTUS Empirical Local Speed [m/s]`;
- `VENTUS Empirical Pressure Ratio` (0–1), weighted using squared transmissivity;
- `VENTUS Empirical Local Pressure [Pa]` (0.5 × air density × weighted local speed²).

It does **not** account for speed-up around obstacles, wake recovery, turbulence, Reynolds effects, pressure fluctuations, roughness-layer physics, changing crown shape, or dynamic structural loading. Meshes and point clouds both use an occupied-voxel representation.

### Level 3 — Future / not implemented

A future CFD backend could provide velocity vectors, signed pressures, turbulence, and genuine aerodynamic loads. No CFD quantities are claimed by this release. The standalone `ventus::Geometry`, `ventus::Options`, `ventus::Results` and CloudCompare adaptation provide interfaces to support a future alternative solver.

## Coordinate and input conventions

- `+X` East, `+Y` North, `+Z` Up; **spatial units MUST be meters**.
- Wind bearings describe where the wind **comes from**: 0° North, 90° East, 180° South, 270° West.
- Wind direction is an incoming **horizontal** bearing; the angular fan adds specified positive/negative ray elevations.
- Wind rose CSV must be `from_deg,frequency,speed_mps` (header optional); frequencies may be percentages or relative weights and are normalized.
- Wind rose is aggregated climate information, not a chronological wind time series. No gust extremes or temporal autocorrelation are estimated.
- The first occupied voxel containing the receiver is skipped to prevent false immediate self-blocking. This leaves an uncertainty of about one voxel near the sample point.

## Naming and migration

**Use `qVENTUS` consistently:** `qVENTUS/`, `qVENTUS.cpp`, `qVENTUS.h`, `qVENTUS.qrc`, `PLUGIN_STANDARD_QVENTUS`, `QVENTUS_PLUGIN`, `libQVENTUS_PLUGIN.so`. The CloudCompare display name is **VENTUS**, and the repository is `ventus-cc`. Earlier v0.1.0 previews used a misspelled internal identifier. To migrate an existing checkout, use:

```bash
python3 tools/link_into_cloudcompare.py ~/dev/CloudCompare --migrate-legacy --replace-copy
```

The migration preserves copied legacy sources as backups and replaces only recognized old development symlinks. See [DEVELOPMENT.md](DEVELOPMENT.md) for the local build and how to remove stale plugin binaries.

## qSOLIS-style local development

If you use the `~/dev/CloudCompare/build-dev` setup with external plugin symlinks, see **[DEVELOPMENT.md](DEVELOPMENT.md)**. The supported symlink command is `python3 tools/link_into_cloudcompare.py ~/dev/CloudCompare`. This keeps core sources in the live repository and allows both qSOLIS and VENTUS in one CloudCompare build.

## Installing the CloudCompare plugin (source)

Requirements: CloudCompare **2.14+ source**, Qt **6** development libraries, CMake, and a C++17 compiler.

```bash
# Have an existing CloudCompare source checkout first.
cd /path/to/ventus-cc
python3 tools/install_into_cloudcompare.py /path/to/CloudCompare

cmake -S /path/to/CloudCompare -B /path/to/CloudCompare/build-ventus \
  -DPLUGIN_STANDARD_QVENTUS=ON -DOPTION_BUILD_CCVIEWER=OFF
cmake --build /path/to/CloudCompare/build-ventus --parallel
cmake --install /path/to/CloudCompare/build-ventus
```

Use `--force` to refresh an existing qVENTUS installation from this source tree. This modifies the CloudCompare Standard plugin `CMakeLists.txt` by appending `add_subdirectory( qVENTUS )` if needed and installs a copy of `qVENTUS` plus its C++ core sources. Use a CloudCompare build and install prefix appropriate for your platform. Binary plugin installation requires an ABI-matching CloudCompare/Qt build.

CloudCompare menu: **Plugins → VENTUS → VENTUS Calculate Wind Exposure...**. Select one or more point clouds / meshes. The plugin also offers **VENTUS Export Crown Metrics...** and **VENTUS Cleanup Results...**. Selected meshes take precedence over their associated vertex clouds.

The GUI provides **target only**, **selected entities**, and **all visible** occlusion contexts. It can recalculate the target alone to estimate neighbour shelter contribution. Crown results are stored in `qVENTUS/crownExposure/latest` JSON metadata on the point/vertex cloud and persist when saved in a CloudCompare format that retains object metadata. The export action writes the selected crowns to CSV, plus a vertical profile CSV.

## Standalone build and testing (does not need CloudCompare)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Standalone CSV batch interface

```bash
./build/ventus-cli --target examples/tree_points.csv \
  --obstacle examples/windbreak_points.csv \
  --rose examples/wind_rose.csv --voxel 0.5 --range 25 \
  --compare-neighbours --output-prefix test-ventus
```

Outputs `test-ventus-points.csv`, `test-ventus-crown.csv`, and `test-ventus-profile.csv`. CLI accepts `--direction`, `--speed` instead of `--rose`, and options shown by `--help`. With `--compare-neighbours` it additionally calculates target-only CWEI and exports the crown-level WEI reduction caused by the added obstacles. **CloudCompare `-VENTUS` integration is now source-implemented** for BIN-based headless jobs (see the CloudCompare CLI guide); use the standalone CSV CLI for fast core tests.

## Important limitations / scientific interpretation

1. **Visibility is not airflow.** The geometric WEF, DWE, WEI, and WPWE are obstruction proxies, not wind-speed or drag measurements.
2. **Voxelization matters.** Sparse TLS point clouds, occluded scan sectors, voxel size, sampling distance, and scan density all alter occupancy and shelter estimates. Mesh triangles are surface-sampled and are not exact conservative voxelizations.
3. **Porosity:** The `relativeSilhouetteDeficit` in the crown CSV is only the difference between wind-rose-weighted and maximum projected silhouettes. It is NOT a measured plant area density, aerodynamic porosity, or a canopy gap fraction. `WEF` is directional ray openness, also not physical aerodynamic porosity.
4. **Projected crown area** is a voxel/pixel-union silhouette approximation, rather than the effective aerodynamic drag area. Projected-area calculations do not remove internal crown gaps due to occlusion automatically.
5. **Shelter angle** is the largest blocked sampled angle (quantized to angular resolution), not a continuously optimized topographic Sx or skyline maximum.
6. **Obstruction distances are censored.** A no-hit ray receives the configured maximum range, which must not be interpreted as a known obstacle at that distance.
7. **Level 2** models ONLY reductions in wind speed; predicted local speed cannot exceed reference speed. Choose `k` based on independent validation and never interpret the outputs as engineering-grade pressures.
8. **Wind load potential** (`m4/s2`) is area × speed² and is **not** a force; the tool does not compute tree stability or windthrow probability.
9. Per-point computations scale with `(points × directions × elevations × distance/voxel)`; downsample and use coarser voxels on large scenes before full-resolution runs.
10. Metadata stores **latest** crown summary only; scalar fields are uniquely named to avoid overwriting old runs. Cleanup removes all VENTUS fields for selected entities.

## Repository structure

```text
ventus-cc/
├── CMakeLists.txt                # standalone core, CLI and tests
├── core/include/ventus/Engine.h  # engine interface
├── core/src/Engine.cpp          # sparse voxels, sampling, attenuation, metrics
├── qVENTUS/                    # CloudCompare/Qt 6 standard plugin
├── cli/main.cpp                 # headless XYZ CSV batch runner
├── tests/test_engine.cpp        # geometric + empirical unit tests
├── examples/                   # sample wind rose and scene
├── tools/install_into_cloudcompare.py
└── docs/MODELS.md               # precise output definitions and roadmap
```

## Reference, authorship and licensing

The CloudCompare plugin pattern and output organization were informed by **[qSOLIS](https://github.com/dabasler/solis-cc)** and CloudCompare's standard-plugin interface. The implementation is independently written. GPL-2.0-or-later applies to this source repository; the full license text is available from the [GNU GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).

If you publish this project under your account, name the remote repository **`ventus-cc`** and keep the plugin's C++ class/identifier `qVENTUS`; CloudCompare will display the plugin name **VENTUS** from `info.json`.

## Wytham CLI regression sweep

`tools/run_ventus_wytham_regression.py` automates parameter sweeps using the
standalone CSV interface (not the CloudCompare CLI). Export a target tree and
optional neighbours as separate CSV files with **X,Y,Z columns in metres** and
the **same global coordinate system**. Downsample large TLS scenes first.

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --parallel
python3 tools/run_ventus_wytham_regression.py \
  --cli ./build/ventus-cli \
  --target tests/data/wytham/target.csv \
  --obstacle tests/data/wytham/neighbours.csv \
  --out tests/results/wytham-sweep
```

This runs k=0/0.15/0.5/1.0 at voxel 0.5 m, voxel-size 0.25/0.5/1.0 m at
k=1, elevation sampling 3/7/11, plus target-vs-neighbours when supplied.
It creates `summary.csv`, `checks.txt` and per-case `*-points.csv`,
`*-crown.csv`, `*-profile.csv`. It verifies point-level geometric invariance,
monotonic attenuation and added-obstacle shelter, and k=0 point/crown speed
and pressure ratios. Voxel and angular sensitivity are descriptive, not
monotonic tests. The empirical model is not CFD.


## Native CloudCompare BIN command-line mode (v0.2.0-dev)

The plugin now registers the `-VENTUS` command for CloudCompare CLI. It supports exact target-entity selection, surrounding-tree context, wind direction/rose inputs, native BIN exports with VENTUS scalar fields, and crown/point CSV exports. See [docs/CLOUDCOMPARE_CLI.md](docs/CLOUDCOMPARE_CLI.md) for usage and limitations. The standalone CLI remains CSV-only. The CloudCompare-specific plugin build has not yet been validated locally.
