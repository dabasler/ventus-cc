> **Update (0.2.0-dev):** Native CloudCompare `-VENTUS` source, target name selection, BIN export and native-BIN regression runner are now implemented. See `docs/CLOUDCOMPARE_CLI.md`. Actual CloudCompare plugin compilation and runtime verification remain pending. Older 0.1.0 notes below describe the previous source release.

# VENTUS 0.1.0 — implementation status

**Local implementation completed:** independent C++17 core, Qt 6/CloudCompare Standard plugin adapter, dialog, scalar field assignment, crown JSON metadata, crown/vertical-profile CSV export, cleanup action, selected/target-only/visible context handling, neighbour-only delta calculation, standalone CSV CLI, installer, and cross-platform CMake/CTest configuration.

**Validated in the available environment:**

- GCC 14.2 standalone engine/CLI compilation and CTest assertions: PASS.
- Clang standalone engine/CLI compilation with AddressSanitizer and UndefinedBehaviorSanitizer: PASS.
- End-to-end CSV CLI run on example target + windbreak geometry + wind rose: PASS; files in `examples/demo-*`.
- Both CloudCompare-source installers and qVENTIS → qVENTUS migration tested against simulated Standard plugin directories: PASS.
- qVENTUS CMake symlink configuration with a mock CloudCompare/Qt interface: PASS (not a real Qt or CloudCompare build).

**NOT yet validated:**

- Actual Qt 6 / CloudCompare 2.14+ compilation, dynamic plugin loading, GUI, metadata persistence through a CC project save/load, Qt resource/icon loading, cross-platform plugin binary packaging. No matching full CloudCompare+Qt source build is installed in this environment.
- Field measurements or CFD comparisons to calibrate empirical attenuation coefficient `k`.
- Large TLS/ALS dataset performance or scientific reproducibility against real forest scans.

**Intentionally deferred:** CloudCompare native `-VENTUS` CLI command, measured vegetation drag models, wind time-series parsing, gust statistics, wake acceleration/recovery, force/moment estimates, windthrow risk, CFD coupling, automated CloudCompare integration CI. The `ventus-cli` standalone tool supports reproducible XYZ CSV jobs immediately.

This is a **working, core-tested source preview**, not a verified CloudCompare release binary. The next validation step is compiling qVENTUS against a real CloudCompare 2.14+ checkout, then smoke-testing GUI selection, scalar fields and CSV exports.
