#include "ventus/Engine.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ventus;
static bool near(double a,double b,double eps=1e-6){return std::abs(a-b)<eps;}
static void testGrid() {
    VoxelGrid grid(1.0);
    grid.add(Geometry{{{2,0,0},{2,0,1}}});
    auto blocked=grid.trace({0.5,0.5,0.5},{1,0,0},10);
    assert(blocked.blocked);assert(near(blocked.firstHitM,1.5));assert(near(blocked.occupiedLengthM,1));
    assert(!grid.trace({0.5,0.5,0.5},{-1,0,0},10).blocked);
    // own receiver cell ignored
    VoxelGrid own(1);own.add(Geometry{{{0,0,0}}});
    assert(!own.trace({0.2,0.2,0.2},{1,0,0},5).blocked);
    // diagonal passes through occupied cell; path length is diagonal length
    VoxelGrid diagonal(1);diagonal.add(Geometry{{{1,1,1}}});
    auto hit=diagonal.trace({0.5,0.5,0.5},{1,1,1},4);
    assert(hit.blocked);assert(near(hit.occupiedLengthM,std::sqrt(3.)));
}
static void testOpenAndSheltered() {
    Options o;o.voxelSize=.5;o.maxDistance=10;o.elevationSamples=1;o.minElevationDeg=o.maxElevationDeg=0;o.sectorHalfWidthDeg=0;
    Geometry target{{{0,0,0}}};
    VoxelGrid open(.5);open.add(target);
    std::vector<RoseBin> rose{{270,1,10}};
    auto a=calculate(target,open,rose,o);
    assert(near(a.points[0].wei,1));assert(near(a.points[0].wpwe,1));
    assert(near(a.points[0].speedRatio,1));assert(near(a.points[0].localSpeedMps,10));
    assert(near(a.crown.speedRatio,1));assert(near(a.crown.pressureRatio,1));
    assert(near(a.points[0].localPressurePa,61.25));
    assert(near(a.crown.exposureP50,1)); // tests percentile integer interpolation
    // solid obstacle WEST of receiver; wind FROM west (270 degrees)
    VoxelGrid sheltered(.5);sheltered.add(target);
    sheltered.add(Geometry{{{-2,0,0},{-2.5,0,0}}});
    auto b=calculate(target,sheltered,rose,o);
    assert(near(b.points[0].dwe,0));assert(near(b.points[0].wei,0));
    assert(b.points[0].speedRatio<1);
    assert(b.points[0].localPressurePa<a.points[0].localPressurePa);
    assert(near(b.points[0].obstructionDistanceM,1.5));
    assert(b.crown.cwei<a.crown.cwei);
    assert(b.points[0].wef<1);
    assert(near(b.points[0].wef+b.points[0].wsf,1));
}
static void testWindClimateWeighting() {
    Options o;o.voxelSize=.5;o.maxDistance=10;o.elevationSamples=1;
    o.minElevationDeg=o.maxElevationDeg=0;o.sectorHalfWidthDeg=0;
    Geometry tree{{{0,0,0}}};
    VoxelGrid grid(.5);grid.add(tree);grid.add(Geometry{{{-2,0,0}}});
    auto rose=std::vector<RoseBin>{{0,1,2},{270,3,10}};
    auto a=calculate(tree,grid,rose,o);
    assert(near(a.points[0].wei,.25));
    assert(near(a.points[0].wpwe,4.0/(4.0+300.0)));
    assert(near(a.points[0].wefreq,.25));
    assert(near(a.points[0].dwe,0)); // dominant frequency is western
    assert(a.points[0].upwindOccupiedPathM>0);
    assert(a.crown.meanOccupiedUpwindPathM>0);
    assert(a.crown.crownLoadPotential>=0);
    o.attenuationPerMeter=0;
    auto noAttenuation=calculate(tree,grid,rose,o);
    assert(near(noAttenuation.points[0].speedRatio,1));
    assert(near(noAttenuation.points[0].pressureRatio,1));
    assert(near(noAttenuation.crown.speedRatio,1));
    assert(near(noAttenuation.crown.pressureRatio,1));
}
static void testWeightsAndRose() {
    Geometry g{{{0,0,0},{.01,0,0},{2,0,0}}};
    auto w=equalVoxelWeights(g,.5);
    assert(near(w[0],.5)&&near(w[1],.5)&&near(w[2],1));
    Geometry tri{{{0,0,0},{1,0,0},{0,1,0}},{{0,1,2}}};
    auto areas=meshVertexAreaWeights(tri);
    assert(near(areas[0],1./6));
    VoxelGrid grid(.2);grid.add(tri);assert(grid.size()>3);
    std::string path="ventus_test_wind.csv";
    {std::ofstream f(path);f<<"from_deg,frequency,speed_mps\n0,25,5\n270,75,12\n";}
    auto rose=loadWindRoseCSV(path);assert(rose.size()==2);assert(near(rose[1].frequency,75));
    bool thrown=false;try{validate(Options{},{{0,0,2}});}catch(const std::invalid_argument&){thrown=true;}
    assert(thrown);
}
int main(){testGrid();testOpenAndSheltered();testWindClimateWeighting();testWeightsAndRose();std::cout<<"VENTUS core tests passed\n";}
