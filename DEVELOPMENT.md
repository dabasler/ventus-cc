# VENTUS development setup (qSOLIS-style)

Repository: `~/dev/ventus-cc` (`/home/db/dev/ventus-cc`)  
CloudCompare checkout: `~/dev/CloudCompare`  
Shared CloudCompare build tree: `~/dev/CloudCompare/build-dev`  
Installed runtime: `~/dev/CloudCompare-install-dev/bin/CloudCompare`  
Plugin source: `~/dev/ventus-cc/qVENTUS`  
Installed source link: `~/dev/CloudCompare/plugins/core/Standard/qVENTUS -> ~/dev/ventus-cc/qVENTUS`

**Plugin identifier:** `qVENTUS` / CMake option `PLUGIN_STANDARD_QVENTUS` / build target `QVENTUS_PLUGIN`  
**Displayed name:** VENTUS (defined in `qVENTUS/info.json`)  
**Version:** `0.2.0-dev` (see `VERSION`, CMake, and `info.json`)

## First-time setup

Place the ZIP in `~/Downloads` and extract it:

```bash
mkdir -p ~/dev
unzip ~/Downloads/VENTUS-qVENTUS-v0.2.0-dev-native-BIN-CLI.zip -d ~/dev
cd ~/dev/ventus-cc
python3 tools/link_into_cloudcompare.py ~/dev/CloudCompare
```

If you previously installed the **incorrectly named `qVENTIS`** plugin using an earlier archive, first run this migration command instead:

```bash
python3 tools/link_into_cloudcompare.py ~/dev/CloudCompare \
  --migrate-legacy --replace-copy
```

The migration removes an old development symlink if it points into this repository, backs up any copied old `qVENTIS` directory, removes `add_subdirectory( qVENTIS )` from the Standard plugin CMake file, and adds the correct `qVENTUS` entry. It does **not** delete any old copied source. If the old symlink points outside this repository, it stops for manual review.

If you only have an existing `qVENTUS` **copied** directory, without the old name, use `--replace-copy` alone.

This moves the previous copied plugin directory to a timestamped backup and creates a live development symlink. It also registers `add_subdirectory( qVENTUS )` in the Standard plugins' CMakeLists.txt if needed. The plugin CMake file resolves its shared core sources from the actual repository directory even when the plugin is accessed through the CloudCompare symlink. Do not also use the copy installer for routine edits.

## Configure and compile with qSOLIS enabled

```bash
cmake -S ~/dev/CloudCompare -B ~/dev/CloudCompare/build-dev \
  -U PLUGIN_STANDARD_QVENTIS \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/dev/CloudCompare-install-dev" \
  -DOPTION_BUILD_CCVIEWER=OFF \
  -DPLUGIN_STANDARD_QSOLIS=ON \
  -DPLUGIN_STANDARD_QVENTUS=ON \
  -DBUILD_TESTING=ON \
  -DSOLIS_BUILD_TESTS=ON

# Build just VENTUS and its dependencies, or the full application if desired:
cmake --build ~/dev/CloudCompare/build-dev --target QVENTUS_PLUGIN --parallel
cmake --build ~/dev/CloudCompare/build-dev --parallel

# Install everything into the existing development runtime:
cmake --install ~/dev/CloudCompare/build-dev --prefix ~/dev/CloudCompare-install-dev

# Run the installed application:
~/dev/CloudCompare-install-dev/bin/CloudCompare
```

If you only built the VENTUS target, the install step installs the existing CloudCompare outputs from the build tree as well. Run a full build first if necessary. Avoid mixing binaries compiled against different CloudCompare or Qt versions.

## Standalone core tests

The VENTUS standalone tests are **not** registered as `qventus_*` tests in CloudCompare's CTest tree. Build the core and test runner separately:

```bash
cd ~/dev/ventus-cc
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R '^ventus-core$'
```

Example end-to-end CLI run:

```bash
cd ~/dev/ventus-cc
./build/ventus-cli \
  --target examples/tree_points.csv \
  --obstacle examples/windbreak_points.csv \
  --rose examples/wind_rose.csv \
  --voxel 0.5 --range 25 --compare-neighbours \
  --output-prefix build/demo-ventus
```

Inspect output files `build/demo-ventus-points.csv`, `build/demo-ventus-crown.csv`, and `build/demo-ventus-profile.csv`. The CloudCompare native CLI (`-VENTUS`) is implemented in source as of 0.2.0-dev (see `docs/CLOUDCOMPARE_CLI.md`); it must still be compiled and tested against your local CloudCompare checkout. The standalone CLI supports CSV datasets.

## Check the build and plugin installation

```bash
cmake -LA -N ~/dev/CloudCompare/build-dev | grep -E 'PLUGIN_STANDARD_(QSOLIS|QVENTUS)'
readlink -f ~/dev/CloudCompare/plugins/core/Standard/qVENTUS
find ~/dev/CloudCompare-install-dev -type f -iname '*VENTUS*'
QT_DEBUG_PLUGINS=1 ~/dev/CloudCompare-install-dev/bin/CloudCompare
```

The expected Linux plugin library name is typically `libQVENTUS_PLUGIN.so` and it should be located under `lib/cloudcompare/plugins/` in the install prefix, depending on your CloudCompare build/install configuration.

If CMake fails with an `add_subdirectory` error, make sure no active `add_subdirectory( qVENTIS )` remains. Then ensure `~/dev/CloudCompare/plugins/core/Standard/CMakeLists.txt` contains `add_subdirectory( qVENTUS )` *once* and that the link points to `~/dev/ventus-cc/qVENTUS`.

## Workflow status

- `.github/workflows/release.yml`: **not yet present**. The repository includes only `.github/workflows/core-ci.yml` for independent C++ core tests.
- CloudCompare plugin binary and Linux/Windows/macOS release packaging: **not validated**.
- CloudCompare integrated `qventus_*` regression tests: **not yet implemented**.
- Standalone core tests: implemented (`tests/test_engine.cpp`, CTest name `ventus-core`).
- `VERSION`: `0.2.0-dev`.
- Initial Git workflow: work in `~/dev/ventus-cc`, then `git init`, commit, and link to the GitHub repository `dabasler/ventus-cc` **once created**.

**Important:** The CloudCompare Qt plugin source has not yet been compiled and run against your local CloudCompare checkout. These instructions set up the intended build; resolve any first-time API/compiler errors before considering the plugin validated.

## Repository directory migration (older development archives)

If you extracted the corrected ZIP **over** an existing `~/dev/ventus-cc`, the old `~/dev/ventus-cc/qVENTIS` directory may remain from the previous archive (ZIP extraction does not delete old files). Once you confirm `qVENTUS/` exists, you can preserve that old folder under another name:

```bash
cd ~/dev/ventus-cc
if [ -d qVENTIS ] && [ ! -L qVENTIS ]; then mv qVENTIS qVENTIS.old-source-backup; fi
```

Only `qVENTUS/` is compiled in the corrected release. The old directory and its optional backup must **not** be registered in CloudCompare. If a previous `libQVENTIS_PLUGIN.so` is still installed in the runtime, move it out of the plugin directory manually before testing; CMake install does not remove obsolete libraries.
