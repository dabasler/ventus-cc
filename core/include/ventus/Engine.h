#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ventus {

struct Vec3 { double x=0, y=0, z=0; };
struct Triangle { std::uint32_t a=0, b=0, c=0; };
struct Geometry {
    std::vector<Vec3> points;
    std::vector<Triangle> triangles;  // optional; otherwise points are voxelized
};
struct RoseBin {
    double fromDeg=270;  // meteorological FROM convention: 0=N, 90=E
    double frequency=1;  // relative weights, not necessarily percentages
    double speed=5;      // reference mean or characteristic speed in m/s
};
struct Options {
    double voxelSize=0.50;            // m
    double maxDistance=50.0;          // m; no hit is censored at this distance
    int azimuthSamples=16;            // complete-circle sampling for WEF
    int elevationSamples=3;           // fan of upwind elevations
    double minElevationDeg=0.0;       // upwind angular fan
    double maxElevationDeg=30.0;
    double sectorHalfWidthDeg=15.0;   // extra horizontal samples (3 per rose direction)
    double exposedThreshold=0.7;     // threshold on directional openness
    double attenuationPerMeter=0.15; // empirical coefficient [m^-1] per occupied voxel path
    double airDensity=1.225;         // kg/m^3
    double crownThreshold=0.7;
    unsigned verticalBands=10;
};

struct PointResult {
    double wef=0, wsf=0, dwe=0, wei=0, wpwe=0, wefreq=0;
    double shelterAngleDeg=0, obstructionDistanceM=0, upwindOpenness=0;
    double upwindOccupiedPathM=0, upwindPathOccupancy=0;
    double speedRatio=1, localSpeedMps=0, localPressurePa=0, pressureRatio=1;
    std::array<double, 8> compassExposure{}; // N,NE,E,SE,S,SW,W,NW
};
struct ProfileBand {
    double zMin=0, zMax=0, wei=0, wpwe=0, speedRatio=0;
    std::size_t count=0;
};
struct Summary {
    double cwei=0, crownPressureExposure=0, meanWef=0, shelter=0;
    double exposureP10=0, exposureP50=0, exposureP90=0, exposedFraction=0;
    double upperExposure=0, lowerExposure=0;
    double speedRatio=1, localSpeedMps=0, pressureRatio=1;
    double crownAsymmetry=0, dominantDirectionDeg=0;
    double maxProjectedAreaM2=0, weightedProjectedAreaM2=0;
    double horizontalGapFraction=0; // equals mean unweighted directional ray openness
    double relativeSilhouetteDeficit=0; // 1 - wind-rose-weighted silhouette / maximum silhouette (NOT true porosity)
    double crownLoadPotential=0; // wind-frequency-weighted projected area * U_ref^2 [m4/s2]
    double meanOccupiedUpwindPathM=0;
    bool neighbourComparisonComputed=false;
    double neighbourShelterDeltaWEI=0; // target only CWEI - context CWEI
    std::array<double, 8> directionalExposure{};
    std::array<double, 8> directionalProjectedAreaM2{};
    std::vector<ProfileBand> verticalProfile;
};
struct Results {
    std::vector<PointResult> points;
    Summary crown;
    std::size_t occupiedCells=0;
};

using Progress = std::function<bool(std::size_t done, std::size_t total)>; // false cancels

// A sparse occupied-voxel grid. Can ingest multiple occluding entities.
// Pure geometry: each occupied cell is completely solid for visibility; Level 2
// integrates occupied path length as a *proxy* for attenuation, not CFD.
class VoxelGrid {
public:
    explicit VoxelGrid(double cellSize);
    void add(const Geometry& geometry);
    std::size_t size() const;
    struct Trace { bool blocked=false; double firstHitM=0, occupiedLengthM=0; };
    Trace trace(Vec3 origin, Vec3 direction, double range) const;
private:
    struct Key { std::int64_t x, y, z; bool operator==(const Key& b) const noexcept; };
    struct Hash { std::size_t operator()(const Key& k) const noexcept; };
    double cell_;
    std::unordered_set<Key, Hash> occupied_;
    Key key(Vec3 p) const;
};

void validate(const Options& opt, const std::vector<RoseBin>& rose);
// Run on target.points; target geometry (including triangles) should also be
// included in the supplied grid to obtain self-sheltering. weights can be
// triangle-vertex areas or voxel-equalized point-cloud weights. An empty vector
// triggers occupied-voxel weights automatically.
Results calculate(const Geometry& target, const VoxelGrid& obstacles,
                  const std::vector<RoseBin>& rose, const Options& opt,
                  const std::vector<double>& weights={}, Progress progress={});
std::vector<double> equalVoxelWeights(const Geometry& target, double voxelSize);
std::vector<double> meshVertexAreaWeights(const Geometry& target);
std::vector<RoseBin> loadWindRoseCSV(const std::string& path);
void exportSummaryCSV(const std::string& path,
                      const std::vector<std::pair<std::string, Summary>>& rows);
void exportPointsCSV(const std::string& path, const Geometry& target, const Results& results);
void exportProfilesCSV(const std::string& path,
                       const std::vector<std::pair<std::string, Summary>>& rows);
} // namespace ventus
