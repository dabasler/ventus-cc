# Wytham Woods test data (not bundled)

VENTUS is developed and tested against segmented terrestrial laser scanning (TLS)
point clouds from Wytham Woods, UK. **No Wytham PLY, BIN, CSV export, plot,
individual tree, or derived point-level results are distributed in this Git repository.**
The point-cloud data are obtained independently by each developer.

## Source and attribution

- Official dataset: **TLS forest instance segmentation benchmark: 2983 manually
  segmented trees from four plots**, by Cherlet et al.
- Zenodo record (v3): https://zenodo.org/records/16875688
- Dataset DOI: https://doi.org/10.5281/zenodo.16875688
- Wytham archive: `wytham.zip` (approximately 3.2 GB as shown on Zenodo).
- The dataset's published license is **Creative Commons Attribution 4.0 (CC BY 4.0)**.
  Follow the license and give the authors and publication appropriate credit.
- Associated article: W. Cherlet et al., *Benchmarking tree instance segmentation of
  terrestrial laser scanning point clouds*, ISPRS Journal of Photogrammetry and
  Remote Sensing 231 (2026), 230–247.
  DOI: https://doi.org/10.1016/j.isprsjprs.2025.10.033

The Wytham scans are *external test data*, not part of VENTUS and not generated
by VENTUS. The file structure/labels may change in later dataset versions.

## Download and local installation (Linux)

Recommended location on db's development workstation (OUTSIDE this Git repo):

```bash
DATA="$HOME/dev/CloudCompare_TestData/wytham"
mkdir -p "$DATA"

# The direct Zenodo file URL serves a multi-GB download; resume if interrupted.
curl -fL --retry 3 -C - \
  'https://zenodo.org/records/16875688/files/wytham.zip?download=1' \
  -o "$HOME/dev/CloudCompare_TestData/wytham.zip"

unzip -q "$HOME/dev/CloudCompare_TestData/wytham.zip" -d "$DATA"
```

Alternatively, open the official Zenodo record in a browser and download
`wytham.zip`. Check the archive's top-level layout after extraction; it may
contain an extra directory. Do not move the original PLY files into `ventus-cc`.

The local test setup reported these relevant files:

```text
~/dev/CloudCompare_TestData/wytham/
  wytham_train.ply
  wytham_val.ply
  wytham_test.ply
  trees/test/in_plot_th0.90/wytham_winter_1119.ply
  ... many other segmented trees ...
```

The three plot PLYs include `instance` and `semantic` properties. The individual
`wytham_winter_1119.ply` has 59,608 points in the local test; it is used for
initial target-only numerical parameter sweeps. Instance labels in whole-plot
files are **not necessarily equal** to numbers appearing in individual filenames.

## Reproduce the initial Wytham single-tree test

The following assumes that the separately provided `tools/ply_to_ventus_xyz.py`
converter has been installed in your working copy. It is not required to
compile the VENTUS core.

```bash
cd "$HOME/dev/ventus-cc"
mkdir -p tests/data/wytham tests/results/wytham

python3 tools/ply_to_ventus_xyz.py \
  "$HOME/dev/CloudCompare_TestData/wytham/trees/test/in_plot_th0.90/wytham_winter_1119.ply" \
  -o tests/data/wytham/target.csv

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/ventus-cli \
  --target tests/data/wytham/target.csv \
  --direction 270 --speed 5 --voxel 0.5 --range 25 \
  --attenuation 0.15 \
  --output-prefix tests/results/wytham/tree1119
```

**All generated `tests/data/`, `tests/results/`, and point-cloud CSV outputs are
ignored by Git.** Only scripts, synthetic fixtures, and explanatory docs belong
in the repository.

Observed development baseline with tree 1119: 59,608 input points and
CWEI ~0.179466 at 0.5 m voxels with the above options. This is a reproducibility
reference for one algorithm version, **not an aerodynamic ground truth**.

Earlier parameter studies tested:

- Attenuation `k = 0, 0.15, 0.5, 1.0` 1/m (mathematical consistency).
- Voxel sizes `0.125, 0.1875, 0.25, 0.35, 0.5, 1.0` m (significant sensitivity).
- Elevation sampling `3, 5, 7, 11` and azimuth sampling `8, 16, 32, 64`.

**Scientific caveat:** The current geometric and empirical wind indices are
not measured wind speeds or forces. Changes in voxel size can materially alter
pressure ratios and projected area. See `docs/MODELS.md`.

## Alternative: native CloudCompare BIN

The CloudCompare plugin supports `-VENTUS -LIST` for BIN entities. A single
loaded cloud is not automatically split into tree instances. Use separate tree
entities or convert a labelled PLY to the standalone CSV format for individual
trials until scalar-field-based instance selection is implemented.
