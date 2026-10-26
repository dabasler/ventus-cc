# VENTUS v0.1.0 — CLI regression additions

- Correct the `Summary::speedRatio` and `Summary::pressureRatio` crown-level
  accumulation bias (`1 / sum of crown point weights`). Previously these
  could be greater than 1 even with attenuation set to 0.
- Add crown-level k=0 assertions to `tests/test_engine.cpp`.
- Add `tools/run_ventus_wytham_regression.py`, a standard-library-only CLI
  driver for repeatable attenuation, voxel, elevation and neighbour-context
  test cases. Produces point/summary/profile CSVs and a pass/fail text report.
- Document the workflow in README.md.

Standalone Linux GCC build, included unit tests and demo regression all pass.
The CloudCompare plugin adapter must still be verified in the user's local build.
