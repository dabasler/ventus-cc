# VENTUS model specification and extension plan

## Geometry sampling

Sparse occupied-voxel grid generated from point positions, and from mesh triangles using regular barycentric sampling. Every occupied voxel is fully blocking for Level 1 rays. A ray never tests its receiver's starting voxel. The ray marching algorithm is a three-dimensional DDA using exact distances through visited voxel cells. The ray range is measured in meters; all angular computations use meteorological incoming bearings.

For incoming wind bearing `theta` measured clockwise from north, the unit horizontal vector from the receiver *toward the upwind direction* is:

`d(theta) = (sin(theta), cos(theta), 0)`.

Elevated rays rotate this vector upward by the sample elevation; the azimuth sector is evaluated at `theta-sectorHalfWidth`, `theta`, `theta+sectorHalfWidth` when the half-width is positive. The elevation fan has `N` equally spaced elevations from `minElevation` through `maxElevation`.

Every direction has geometric openness `E_theta(p) = unblocked rays / rays`. The `maxDistance` query is a finite search. A no-hit ray is open even if an obstruction may exist farther away.

## Level 1 fields

With uniform azimuths `theta_j`, `WEF(p) = mean_j E_theta_j(p)` using zero sector spread. `WSF = 1-WEF`.

Let wind rose bins have frequencies `f_i`, speeds `U_i`, and directional openness `E_i` (with the configured sector spread):

- `WEI = Σ f_i E_i / Σ f_i`.
- `WPWE = Σ f_i U_i² E_i / Σ f_i U_i²`, falling back to WEI if all speeds are zero.
- `WEFreq = Σ f_i I(E_i >= threshold) / Σ f_i`.
- `DWE` evaluates `E_i` for the highest-frequency bin (single-direction input is one bin).
- `ShelterAngle` evaluates the highest **sampled** nonnegative elevation at which at least one ray in DWE sector hits something. This differs from a continuously determined skyline angle.
- `ObstructionDistance` is the average first hit on the **lowest elevation** of the DWE fan; no-hit returns `maxDistance`. It is capped and may represent terrain, crown, or building.
- `UpwindOccupiedPath` integrates occupied voxel chord lengths along the DWE fan. This is a density proxy and may be affected by point cloud coverage.

## Level 2 (explicitly empirical)

For every angular ray `r`, integrate `L_r = Σ occupied chord length [m]`. Define

`T_r = exp(-k L_r)` where `k` has units `m⁻¹`.

The directional transmissivity is `T_i = mean_r T_r`. The estimated long-term mean local speed is

`U_local = Σ f_i U_i T_i / Σ f_i`.

The speed ratio is `R_U = Σ f_i U_i T_i / Σ f_i U_i`, or mean transmissivity when all input speeds are zero.

The empirical local mean dynamic pressure proxy is

`q_local = 1/2 rho Σ f_i U_i² T_i² / Σ f_i`, and pressure ratio

`R_q = Σ f_i U_i² T_i² / Σ f_i U_i²`.

**This is not an aerodynamic solution.** The same model with `k=0` produces `U_local = mean(U_reference)`, speed ratio 1 and pressure ratio 1. Actual airflow can be faster than reference wind, but this empirical shelter model does not represent speed-up.

## Crown metrics

- For clouds, points within the same target voxel get weights `1 / point_count_in_voxel` (total voxel weight = 1).
- For meshes, each triangle contributes one-third of its 3D area to each vertex.
- CWEI is the weighted mean point WEI; crown pressure exposure similarly aggregates WPWE.
- Exposed fraction is the **weighted** fraction of points/vertices whose WEI meets the user threshold.
- P10/P50/P90 are weighted quantiles based on percentile-rank interpolation at observation-weight midpoints.
- Upper/lower are split at the midpoint of the target's minimum and maximum Z, not a biological live crown base.
- Vertical profiles comprise 10 equally sized height bands (configurable in the engine); each reports count, WEI, WPWE and speed ratio.
- For eight compass directions, project target voxels/triangle samples onto a plane perpendicular to the horizontal wind and Z, union into square pixels of width `voxelSize`. Pixel count × `voxelSize²` approximates windward projected crown area [m²]. This is a **2D digital silhouette**, not a calibrated drag area.
- Crown load potential is `Σ f_i A_projected(closest compass bin) U_i² / Σ f_i` [m4/s2]. No aerodynamic drag coefficient is applied.
- Crown asymmetry is the length of the resultant of eight openness values divided by their sum (0 = perfectly balanced).
- Dominant exposure direction is the compass bearing of maximum crown directional openness, NOT the prevailing wind direction.
- `relativeSilhouetteDeficit = 1 - wind-rose-weighted projected area / max projected area`: geometric directional asymmetry proxy, NOT porosity.
- Neighbour shelter delta = CWEI with **target only** geometry minus CWEI with selected/all-visible geometry, normally ≥ 0; it is a comparison of visibility, NOT observed thinning effects. The crown CSV records `NeighbourComparisonComputed` and leaves `NeighbourShelterDeltaWEI` blank if this was not calculated.

## Future Level 3 interface plan

Retain the input scene with a target/occluder split, scalar-field naming and result schema version. Add a solver abstraction, e.g.

```cpp
struct WindSolver {
    virtual ~WindSolver() = default;
    virtual Results calculate(const Geometry&, const Scene&, const WindClimate&, const Options&) = 0;
};
```

Expose additional CFD-only fields with clear units: velocity vector components [m/s], pressure difference [Pa], Reynolds stresses or turbulence kinetic energy [m²/s²], and force/torque from resolved integration. Require explicit mesh generation, boundary conditions, reference wind profile, turbulence closure, convergence diagnostics and numerical validation. Do not relabel Level 2 quantities as CFD-derived.
