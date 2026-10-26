# VENTUS / qVENTUS v0.1.0 — CloudCompare 2.14 compiler fix 1

Reported first real CloudCompare build against Qt 6 / CloudCompare 2.14:

1. `getScalarField()` returns `std::shared_ptr<CCCoreLib::ScalarField>`, so `auto*`/`const auto*` cannot deduce the returned type. `auto`/`const auto` work and keep the shared ownership while in use.
2. The single declaration `const auto opt=dlg.options(),rose=dlg.windRose();` deduces unrelated types; now two declarations.
3. The plugin CMake target now explicitly compiles `qVENTUS.qrc` using `AUTORCC` so `:/CC/plugin/qVENTUS/info.json` and `ventus.svg` are embedded into the plugin.

Apply to an existing qVENTUS working copy:

```bash
cd ~/dev/ventus-cc
patch --dry-run -p1 < ~/Downloads/qVENTUS-CloudCompare-2.14-compile-fix.patch
patch -p1 < ~/Downloads/qVENTUS-CloudCompare-2.14-compile-fix.patch
cmake -S ~/dev/CloudCompare -B ~/dev/CloudCompare/build-dev
cmake --build ~/dev/CloudCompare/build-dev --target QVENTUS_PLUGIN --parallel
cmake --install ~/dev/CloudCompare/build-dev --prefix ~/dev/CloudCompare-install-dev
```

Validation in this environment: GCC/Ninja standalone core build, CTest core test, 5 setup-script tests, and CLI sample run passed. The actual CloudCompare/Qt plugin binary could not be compiled here because those development dependencies are not available in this environment.
