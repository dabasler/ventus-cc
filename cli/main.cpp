#include "ventus/Engine.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
ventus::Geometry readCloud(const std::string& path) {
    std::ifstream in(path);if(!in)throw std::runtime_error("cannot open cloud: "+path);
    ventus::Geometry g;std::string line;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#')continue;
        for(char& c:line)if(c==';'||c==',')c=' ';
        std::istringstream row(line);ventus::Vec3 p;
        if(row>>p.x>>p.y>>p.z)g.points.push_back(p);
        // header lines are ignored; other malformed rows are rejected
        else if(line.find("x")!=std::string::npos||line.find("X")!=std::string::npos) continue;
        else throw std::runtime_error("malformed XYZ data row in "+path);
    }
    if(g.points.empty())throw std::runtime_error("empty cloud: "+path);
    return g;
}
void usage() {
    std::cout<<"VENTUS standalone CSV batch analyzer (meters; X east, Y north, Z up)\n"
             <<"Usage: ventus-cli --target cloud.csv [--obstacle context.csv] [--rose rose.csv]\n"
             <<"       [--direction 270] [--speed 5] [--voxel 0.5] [--range 50]\n"
             <<"       [--attenuation 0.15] [--azimuth-samples 16] [--elevation-samples 3]\n"
             <<"       [--compare-neighbours] [--output-prefix ventus-output]\n"
             <<"Cloud: x,y,z CSV; rose: from_deg,frequency,speed_mps; context is in addition to target.\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==1){usage();return 1;}
        ventus::Options opt;std::string targetPath,rosePath,prefix="ventus-output";
        std::vector<std::string> obstaclePaths;
        double direction=270,speed=5;
        bool compareNeighbours=false;
        for(int i=1;i<argc;++i) {
            std::string name=argv[i];
            if(name=="--help"||name=="-h"){usage();return 0;}
            if(name=="--compare-neighbours"){compareNeighbours=true;continue;}
            if(i+1>=argc)throw std::runtime_error("missing value for "+name);
            std::string val=argv[++i];
            if(name=="--target")targetPath=val;
            else if(name=="--obstacle")obstaclePaths.push_back(val);
            else if(name=="--rose")rosePath=val;
            else if(name=="--output-prefix")prefix=val;
            else if(name=="--direction")direction=std::stod(val);
            else if(name=="--speed")speed=std::stod(val);
            else if(name=="--voxel")opt.voxelSize=std::stod(val);
            else if(name=="--range")opt.maxDistance=std::stod(val);
            else if(name=="--attenuation")opt.attenuationPerMeter=std::stod(val);
            else if(name=="--azimuth-samples")opt.azimuthSamples=std::stoi(val);
            else if(name=="--elevation-samples")opt.elevationSamples=std::stoi(val);
            else throw std::runtime_error("unknown argument: "+name);
        }
        if(targetPath.empty())throw std::runtime_error("--target is required");
        const auto target=readCloud(targetPath);
        ventus::VoxelGrid grid(opt.voxelSize);grid.add(target);
        for(const auto& p:obstaclePaths)grid.add(readCloud(p));
        auto rose=rosePath.empty()?std::vector<ventus::RoseBin>{{direction,1,speed}}:ventus::loadWindRoseCSV(rosePath);
        auto results=ventus::calculate(target,grid,rose,opt);
        if(compareNeighbours) {
            ventus::VoxelGrid self(opt.voxelSize);self.add(target);
            const auto targetOnly=ventus::calculate(target,self,rose,opt);
            results.crown.neighbourComparisonComputed=true;
            results.crown.neighbourShelterDeltaWEI=targetOnly.crown.cwei-results.crown.cwei;
        }
        ventus::exportPointsCSV(prefix+"-points.csv",target,results);
        ventus::exportSummaryCSV(prefix+"-crown.csv",{{targetPath,results.crown}});
        ventus::exportProfilesCSV(prefix+"-profile.csv",{{targetPath,results.crown}});
        std::cout<<"VENTUS: "<<target.points.size()<<" points, "<<grid.size()<<" occupied cells\n"
                 <<"CWEI="<<results.crown.cwei<<" WPWE="<<results.crown.crownPressureExposure
                 <<" speed ratio="<<results.crown.speedRatio<<"\n"
                 <<"CSV: "<<prefix<<"-{points,crown,profile}.csv\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<"VENTUS error: "<<e.what()<<'\n';return 2;}
}
