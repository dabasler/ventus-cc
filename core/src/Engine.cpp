#include "ventus/Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ventus {
namespace {
constexpr double PI = 3.14159265358979323846;
double rad(double x) { return x*PI/180.0; }
double clamp(double v, double a=0, double b=1) {return std::max(a,std::min(v,b));}
double norm(Vec3 a) {return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
Vec3 minus(Vec3 a, Vec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 cross(Vec3 a, Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 direction(double fromDeg, double elevationDeg) {
    const double a=rad(fromDeg), e=rad(elevationDeg);
    return {std::sin(a)*std::cos(e), std::cos(a)*std::cos(e), std::sin(e)};
}
double normalizedAngle(double a) {a=std::fmod(a,360.0);return a<0?a+360.0:a;}
double weightedPercentile(const std::vector<double>& values,const std::vector<double>& weights,double p) {
    if(values.empty()) return 0;
    std::vector<std::pair<double,double>> sorted;
    for(size_t i=0;i<values.size();++i)
        if(weights[i]>0 && std::isfinite(values[i]) && std::isfinite(weights[i]))
            sorted.emplace_back(values[i],weights[i]);
    if(sorted.empty()) return 0;
    std::sort(sorted.begin(),sorted.end());
    double total=0;for(const auto& v:sorted)total+=v.second;
    double c=0,prevCentre=0,prevValue=sorted.front().first;
    for(const auto& v:sorted) {
        double centre=(c+0.5*v.second)/total;
        if(p<=centre) {
            if(c==0)return v.first;
            const double t=clamp((p-prevCentre)/(centre-prevCentre));
            return prevValue*(1-t)+v.first*t;
        }
        c+=v.second;prevCentre=centre;prevValue=v.first;
    }
    return sorted.back().first;
}
struct AngleResult {
    double openness=0, transmission=0, firstHit=0, shelterAngle=0, occupiedPathM=0;
};
AngleResult evaluate(const VoxelGrid& grid, Vec3 p, double fromDeg, const Options& opt,
                     double spreadDeg) {
    AngleResult r;
    const int lateral=spreadDeg>0?3:1;
    const int n=opt.elevationSamples*lateral;
    double angleStep=opt.elevationSamples>1 ?
        (opt.maxElevationDeg-opt.minElevationDeg)/(opt.elevationSamples-1):0;
    double distanceSum=0;
    for (int a=0;a<lateral;++a) {
        const double az=fromDeg + (lateral==1?0:(a-1)*spreadDeg);
        for (int e=0;e<opt.elevationSamples;++e) {
            const double elev=opt.minElevationDeg+e*angleStep;
            auto tr=grid.trace(p,direction(az,elev),opt.maxDistance);
            const double open=tr.blocked?0.:1.;
            r.openness+=open;
            r.transmission+=std::exp(-opt.attenuationPerMeter*tr.occupiedLengthM);
            r.occupiedPathM+=tr.occupiedLengthM;
            if(tr.blocked && elev>=0) r.shelterAngle=std::max(r.shelterAngle,elev);
            if(e==0) distanceSum+=tr.blocked?tr.firstHitM:opt.maxDistance;
        }
    }
    r.openness/=n;
    r.transmission/=n;
    r.occupiedPathM/=n;
    r.firstHit=distanceSum/lateral; // distance for lowest sampled elevation; censored
    return r;
}
struct PairKey {
    std::int64_t x=0,y=0;
    bool operator==(PairKey b)const {return x==b.x&&y==b.y;}
};
struct PairHash {
    size_t operator()(PairKey k)const {
        auto h=static_cast<std::uint64_t>(k.x)*0x9e3779b97f4a7c15ULL;
        h^=static_cast<std::uint64_t>(k.y)*0xc2b2ae3d27d4eb4fULL;
        return static_cast<size_t>(h^(h>>32));
    }
};
double projectedArea(const Geometry& target, double voxel, double fromDeg) {
    // Occupied silhouette pixels: union projection. Approximate, resolution dependent.
    std::unordered_set<PairKey,PairHash> pixels;
    const double a=rad(fromDeg), ux=std::cos(a), uy=-std::sin(a);
    const double inv=1./voxel;
    for (const auto& p:target.points) {
        pixels.insert({static_cast<std::int64_t>(std::floor((p.x*ux+p.y*uy)*inv)),
                       static_cast<std::int64_t>(std::floor(p.z*inv))});
    }
    // Triangle raster surface sampling is done for a mesh in a separate pass.
    for(const auto& t:target.triangles) {
        if(t.a>=target.points.size()||t.b>=target.points.size()||t.c>=target.points.size())continue;
        Vec3 p=target.points[t.a], q=target.points[t.b], r=target.points[t.c];
        double sampleCount=std::ceil(std::max({norm(minus(p,q)),norm(minus(p,r)),norm(minus(q,r))})/(voxel*.45));
        int steps=std::max(1,static_cast<int>(std::min(sampleCount,4096.0)));
        for(int i=0;i<=steps;++i)for(int j=0;j<=steps-i;++j) {
            const double u=static_cast<double>(i)/steps, v=static_cast<double>(j)/steps;
            Vec3 b={p.x*(1-u-v)+q.x*u+r.x*v,p.y*(1-u-v)+q.y*u+r.y*v,p.z*(1-u-v)+q.z*u+r.z*v};
            pixels.insert({static_cast<std::int64_t>(std::floor((b.x*ux+b.y*uy)*inv)),
                           static_cast<std::int64_t>(std::floor(b.z*inv))});
        }
    }
    return static_cast<double>(pixels.size())*voxel*voxel;
}
std::string quoted(std::string s) {
    std::string out="\"";
    for(char c:s) {if(c=='\"') out+='\"'; out+=c;}
    return out+'\"';
}
} // namespace

bool VoxelGrid::Key::operator==(const Key& b)const noexcept {return x==b.x&&y==b.y&&z==b.z;}
std::size_t VoxelGrid::Hash::operator()(const Key& k)const noexcept {
    std::uint64_t h=1469598103934665603ULL;
    for (auto a:{k.x,k.y,k.z}) {h^=static_cast<std::uint64_t>(a);h*=1099511628211ULL;}
    return static_cast<std::size_t>(h);
}
VoxelGrid::VoxelGrid(double c):cell_(c) {if(!(c>0)||!std::isfinite(c))throw std::invalid_argument("voxel size must be positive");}
VoxelGrid::Key VoxelGrid::key(Vec3 p)const {
    return {static_cast<std::int64_t>(std::floor(p.x/cell_)),static_cast<std::int64_t>(std::floor(p.y/cell_)),static_cast<std::int64_t>(std::floor(p.z/cell_))};
}
void VoxelGrid::add(const Geometry& g) {
    for (auto p:g.points) occupied_.insert(key(p));
    for(const auto& t:g.triangles) {
        if(t.a>=g.points.size()||t.b>=g.points.size()||t.c>=g.points.size())
            throw std::invalid_argument("triangle index outside geometry");
        Vec3 a=g.points[t.a], b=g.points[t.b], c=g.points[t.c];
        double samples=std::ceil(std::max({norm(minus(a,b)),norm(minus(a,c)),norm(minus(b,c))})/(cell_*0.45));
        if(samples>8192) throw std::invalid_argument("mesh triangle too large for selected voxel size");
        int n=static_cast<int>(samples);
        n=std::max(1,n);
        for(int i=0;i<=n;++i)for(int j=0;j<=n-i;++j) {
            double u=static_cast<double>(i)/n, v=static_cast<double>(j)/n;
            occupied_.insert(key({a.x*(1-u-v)+b.x*u+c.x*v,
                                  a.y*(1-u-v)+b.y*u+c.y*v,
                                  a.z*(1-u-v)+b.z*u+c.z*v}));
        }
    }
}
std::size_t VoxelGrid::size() const {return occupied_.size();}
VoxelGrid::Trace VoxelGrid::trace(Vec3 p, Vec3 d, double range) const {
    Trace out;
    if(occupied_.empty()||range<=0) return out;
    const double len=norm(d);
    if(len<1e-20) return out;
    d={d.x/len,d.y/len,d.z/len};
    Key cell=key(p), initial=cell;
    const std::array<double,3> o={p.x,p.y,p.z},dir={d.x,d.y,d.z};
    std::array<std::int64_t,3> pos={cell.x,cell.y,cell.z};
    std::array<int,3> step{};
    std::array<double,3> tNext{},tDelta{};
    for(int ax=0;ax<3;++ax) {
        if(std::abs(dir[ax])<1e-14) {step[ax]=0;tNext[ax]=tDelta[ax]=std::numeric_limits<double>::infinity();}
        else {
            step[ax]=dir[ax]>0?1:-1;
            double boundary=(static_cast<double>(pos[ax])+(step[ax]>0?1:0))*cell_;
            tNext[ax]=std::max(0.0,(boundary-o[ax])/dir[ax]);
            tDelta[ax]=cell_/std::abs(dir[ax]);
        }
    }
    double previous=0;
    const double eps=cell_*1.e-9;
    // Traverse through first voxel as well, but exclude it from occlusion to
    // avoid immediately hitting the receiver's own occupied cell.
    while(previous<range) {
        double next=std::min({tNext[0],tNext[1],tNext[2],range});
        if(!(next>=previous)) break;
        Key k{pos[0],pos[1],pos[2]};
        if(!(k==initial) && occupied_.find(k)!=occupied_.end()) {
            if(!out.blocked){out.blocked=true;out.firstHitM=previous;}
            out.occupiedLengthM+=next-previous;
        }
        if(next>=range-eps)break;
        previous=next;
        for(int ax=0;ax<3;++ax) if(tNext[ax]<=next+eps) {
            pos[ax]+=step[ax];tNext[ax]+=tDelta[ax];
        }
    }
    return out;
}

void validate(const Options& o,const std::vector<RoseBin>& rose) {
    if(!std::isfinite(o.voxelSize)||!std::isfinite(o.maxDistance)||
       !std::isfinite(o.minElevationDeg)||!std::isfinite(o.maxElevationDeg)||
       !std::isfinite(o.sectorHalfWidthDeg)||!std::isfinite(o.exposedThreshold)||
       !std::isfinite(o.crownThreshold)||!std::isfinite(o.attenuationPerMeter)||
       !std::isfinite(o.airDensity))throw std::invalid_argument("non-finite option");
    if(!(o.voxelSize>0&&o.maxDistance>o.voxelSize&&o.maxDistance<1e6))throw std::invalid_argument("invalid voxel size or maximum range");
    if(o.azimuthSamples<4||o.azimuthSamples>360||o.elevationSamples<1||o.elevationSamples>30)
        throw std::invalid_argument("invalid angular sampling counts");
    if(o.minElevationDeg< -89||o.maxElevationDeg>89||o.maxElevationDeg<o.minElevationDeg)
        throw std::invalid_argument("invalid elevation range");
    if(o.sectorHalfWidthDeg<0||o.sectorHalfWidthDeg>60||o.exposedThreshold<0||o.exposedThreshold>1||
       o.crownThreshold<0||o.crownThreshold>1||o.attenuationPerMeter<0||o.airDensity<=0||o.verticalBands<1||o.verticalBands>100)
        throw std::invalid_argument("invalid empirical or crown parameters");
    if(rose.empty())throw std::invalid_argument("wind rose must have at least one bin");
    double weights=0;
    for(const auto& r:rose){
        if(!std::isfinite(r.fromDeg)||!std::isfinite(r.speed)||!std::isfinite(r.frequency)||r.speed<0||r.frequency<0)
            throw std::invalid_argument("wind rose contains invalid direction, speed, or frequency");
        weights+=r.frequency;
    }
    if(!(weights>0))throw std::invalid_argument("wind rose contains no positive frequency");
}
std::vector<double> equalVoxelWeights(const Geometry& g,double cell) {
    if(!(cell>0))throw std::invalid_argument("invalid voxel size");
    struct VKey {std::int64_t a,b,c;bool operator==(VKey r)const{return a==r.a&&b==r.b&&c==r.c;}};
    struct H {size_t operator()(VKey k)const {std::uint64_t h=1469598103934665603ULL;for(auto v:{k.a,k.b,k.c}){h^=static_cast<std::uint64_t>(v);h*=1099511628211ULL;}return static_cast<size_t>(h);}};
    std::unordered_map<VKey,unsigned,H> counts;
    auto get=[=](Vec3 p)->VKey{return {static_cast<std::int64_t>(std::floor(p.x/cell)),static_cast<std::int64_t>(std::floor(p.y/cell)),static_cast<std::int64_t>(std::floor(p.z/cell))};};
    for(auto p:g.points)++counts[get(p)];
    std::vector<double> result;result.reserve(g.points.size());
    for(auto p:g.points)result.push_back(1./counts.at(get(p)));
    return result;
}
std::vector<double> meshVertexAreaWeights(const Geometry& g) {
    std::vector<double> w(g.points.size(),0);
    for(const auto& t:g.triangles) {
        if(t.a>=w.size()||t.b>=w.size()||t.c>=w.size())throw std::invalid_argument("invalid mesh triangle");
        double area=0.5*norm(cross(minus(g.points[t.b],g.points[t.a]),minus(g.points[t.c],g.points[t.a])));
        w[t.a]+=area/3; w[t.b]+=area/3; w[t.c]+=area/3;
    }
    return w;
}
Results calculate(const Geometry& target,const VoxelGrid& obstacles,const std::vector<RoseBin>& rose,
                  const Options& opt,const std::vector<double>& suppliedWeights,Progress progress) {
    validate(opt,rose);
    if(!suppliedWeights.empty()&&suppliedWeights.size()!=target.points.size())throw std::invalid_argument("weights size mismatch");
    Results results;results.occupiedCells=obstacles.size();results.points.resize(target.points.size());
    if(target.points.empty()) return results;
    std::vector<double> weights=suppliedWeights.empty()?
        (target.triangles.empty()?equalVoxelWeights(target,opt.voxelSize):meshVertexAreaWeights(target)):suppliedWeights;
    double sumW=std::accumulate(weights.begin(),weights.end(),0.0);
    if(sumW<=0) weights.assign(target.points.size(),1.),sumW=target.points.size();
    double sumFreq=0,sumP=0,sumU=0;
    for(auto r:rose) {sumFreq+=r.frequency;sumP+=r.frequency*r.speed*r.speed;sumU+=r.frequency*r.speed;}
    double zMin=target.points.front().z,zMax=zMin;
    for(auto p:target.points){zMin=std::min(zMin,p.z);zMax=std::max(zMax,p.z);}
    auto& cr=results.crown;
    // Accumulate deviations from 1.0 starting at zero, not the public
    // Summary defaults of 1.0 (which would add 1/sumW to crown ratios).
    cr.speedRatio=0.0;
    cr.pressureRatio=0.0;
    cr.verticalProfile.resize(opt.verticalBands);
    double bandHeight=std::max(zMax-zMin,1e-9)/opt.verticalBands;
    for(unsigned i=0;i<opt.verticalBands;++i) {
        cr.verticalProfile[i].zMin=zMin+i*bandHeight;
        cr.verticalProfile[i].zMax=(i+1==opt.verticalBands)?zMax:zMin+(i+1)*bandHeight;
    }
    std::vector<double> bandW(opt.verticalBands,0), upperLowerW(2,0), upperLowerEx(2,0);
    std::vector<double> exposures;exposures.reserve(target.points.size());
    std::array<double,8> compassWeighted{};
    for(size_t j=0;j<target.points.size();++j) {
        if(progress && (j%128==0 || j+1==target.points.size()) && !progress(j,target.points.size()))
            throw std::runtime_error("calculation cancelled");
        auto& r=results.points[j];const Vec3 p=target.points[j];const double w=weights[j];
        for(int k=0;k<opt.azimuthSamples;++k)
            r.wef+=evaluate(obstacles,p,360.*k/opt.azimuthSamples,opt,0).openness/opt.azimuthSamples;
        r.wsf=1-r.wef;
        for(int k=0;k<8;++k){
            r.compassExposure[k]=evaluate(obstacles,p,45.*k,opt,opt.sectorHalfWidthDeg).openness;
            compassWeighted[k]+=w*r.compassExposure[k];
        }
        double roseOpen=0,roseP=0,roseFreq=0,roseTransP=0,roseSpeed=0,roseTrans=0;
        for(const auto& bin:rose) {
            const auto e=evaluate(obstacles,p,bin.fromDeg,opt,opt.sectorHalfWidthDeg);
            const double f=bin.frequency, pweight=f*bin.speed*bin.speed;
            roseOpen+=f*e.openness;
            roseP+=pweight*e.openness;
            roseFreq+=f*(e.openness>=opt.exposedThreshold?1:0);
            roseTransP+=pweight*e.transmission*e.transmission;
            roseSpeed+=f*bin.speed*e.transmission;
            roseTrans+=f*e.transmission;
        }
        const auto dominant=std::max_element(rose.begin(),rose.end(),[](const RoseBin& a,const RoseBin& b){return a.frequency<b.frequency;});
        auto up=evaluate(obstacles,p,dominant->fromDeg,opt,opt.sectorHalfWidthDeg);
        r.dwe=up.openness; r.upwindOpenness=up.openness;
        r.obstructionDistanceM=up.firstHit;r.shelterAngleDeg=up.shelterAngle;
        r.upwindOccupiedPathM=up.occupiedPathM;
        r.upwindPathOccupancy=clamp(up.occupiedPathM/opt.maxDistance);
        r.wei=roseOpen/sumFreq;
        r.wpwe=sumP>0?roseP/sumP:r.wei;
        r.wefreq=roseFreq/sumFreq;
        r.speedRatio=sumU>0?roseSpeed/sumU:roseTrans/sumFreq;
        r.localSpeedMps=roseSpeed/sumFreq;
        r.localPressurePa=.5*opt.airDensity*roseTransP/sumFreq;
        r.pressureRatio=sumP>0?roseTransP/sumP:1;
        exposures.push_back(r.wei);
        cr.cwei+=w*r.wei;cr.crownPressureExposure+=w*r.wpwe;cr.meanWef+=w*r.wef;
        cr.meanOccupiedUpwindPathM+=w*r.upwindOccupiedPathM;
        cr.speedRatio+=w*(r.speedRatio-1);cr.localSpeedMps+=w*r.localSpeedMps;
        cr.pressureRatio+=w*(r.pressureRatio-1);
        if(r.wei>=opt.crownThreshold)cr.exposedFraction+=w;
        unsigned band=std::min(opt.verticalBands-1,static_cast<unsigned>(std::max(0.,(p.z-zMin)/bandHeight)));
        auto& b=cr.verticalProfile[band];b.count++;bandW[band]+=w;b.wei+=w*r.wei;b.wpwe+=w*r.wpwe;b.speedRatio+=w*r.speedRatio;
        int upper=p.z>=zMin+.5*(zMax-zMin)?1:0;
        upperLowerW[upper]+=w;upperLowerEx[upper]+=w*r.wei;
    }
    if(progress&&!progress(target.points.size(),target.points.size()))throw std::runtime_error("calculation cancelled");
    cr.cwei/=sumW;cr.crownPressureExposure/=sumW;cr.meanWef/=sumW;cr.shelter=1-cr.cwei;
    cr.meanOccupiedUpwindPathM/=sumW;
    cr.speedRatio=1+(cr.speedRatio/sumW);cr.localSpeedMps/=sumW;cr.pressureRatio=1+(cr.pressureRatio/sumW);
    cr.exposedFraction/=sumW;
    cr.exposureP10=weightedPercentile(exposures,weights,.1);cr.exposureP50=weightedPercentile(exposures,weights,.5);cr.exposureP90=weightedPercentile(exposures,weights,.9);
    cr.lowerExposure=upperLowerW[0]>0?upperLowerEx[0]/upperLowerW[0]:0;
    cr.upperExposure=upperLowerW[1]>0?upperLowerEx[1]/upperLowerW[1]:0;
    double vx=0,vy=0;
    for(int k=0;k<8;++k) {
        cr.directionalExposure[k]=compassWeighted[k]/sumW;
        vx+=cr.directionalExposure[k]*std::sin(rad(k*45.));
        vy+=cr.directionalExposure[k]*std::cos(rad(k*45.));
        if(cr.directionalExposure[k]>cr.directionalExposure[static_cast<int>(cr.dominantDirectionDeg/45)])
            cr.dominantDirectionDeg=k*45.;
        cr.directionalProjectedAreaM2[k]=projectedArea(target,opt.voxelSize,k*45.);
        cr.maxProjectedAreaM2=std::max(cr.maxProjectedAreaM2,cr.directionalProjectedAreaM2[k]);
    }
    cr.crownAsymmetry=clamp(std::hypot(vx,vy)/(std::accumulate(cr.directionalExposure.begin(),cr.directionalExposure.end(),0.0)+1e-12));
    for(const auto& b:rose) {
        const int k=static_cast<int>(std::round(normalizedAngle(b.fromDeg)/45.))%8;
        cr.weightedProjectedAreaM2+=cr.directionalProjectedAreaM2[k]*b.frequency/sumFreq;
        cr.crownLoadPotential+=cr.directionalProjectedAreaM2[k]*b.frequency*b.speed*b.speed/sumFreq;
    }
    cr.horizontalGapFraction=cr.meanWef; // named approximate; directional geometric gap proxy
    cr.relativeSilhouetteDeficit=1.0-cr.weightedProjectedAreaM2/(cr.maxProjectedAreaM2+1e-12); // NOT physical porosity
    for(unsigned i=0;i<opt.verticalBands;++i) if(bandW[i]>0) {
        cr.verticalProfile[i].wei/=bandW[i];cr.verticalProfile[i].wpwe/=bandW[i];cr.verticalProfile[i].speedRatio/=bandW[i];
    }
    return results;
}
std::vector<RoseBin> loadWindRoseCSV(const std::string& path) {
    std::ifstream in(path);
    if(!in)throw std::runtime_error("cannot open wind rose: "+path);
    std::vector<RoseBin> r;std::string s;
    while(std::getline(in,s)) {
        if(s.empty()||s.front()=='#')continue;
        std::replace(s.begin(),s.end(),';',',');
        std::istringstream line(s);std::string a,b,c;
        if(!std::getline(line,a,',')||!std::getline(line,b,',')||!std::getline(line,c,','))
            throw std::runtime_error("wind rose requires from_deg,frequency,speed_mps columns");
        auto trim=[](std::string& v) {
            auto first=v.find_first_not_of(" \t\r");
            if(first==std::string::npos){v.clear();return;}
            auto last=v.find_last_not_of(" \t\r");v=v.substr(first,last-first+1);
        };
        trim(a);trim(b);trim(c);
        try {size_t p1=0,p2=0,p3=0;double d=std::stod(a,&p1),f=std::stod(b,&p2),u=std::stod(c,&p3);
            if(p1!=a.size()||p2!=b.size()||p3!=c.size())throw std::invalid_argument("non-numeric");
            r.push_back({d,f,u});
        } catch(const std::invalid_argument&) {
            // Only a recognizable header is optional; malformed numeric rows fail.
            if(!r.empty() || (a.find("from") == std::string::npos && a.find("direction") == std::string::npos))throw;
        }
    }
    Options o;validate(o,r);return r;
}
void exportSummaryCSV(const std::string& path,const std::vector<std::pair<std::string,Summary>>& rows) {
    std::ofstream out(path);if(!out)throw std::runtime_error("cannot create summary CSV: "+path);
    out<<std::setprecision(10);
    out<<"entity,CWEI,PressureWeightedExposure,WEF,Shelter,P10,P50,P90,ExposedFraction,UpperExposure,LowerExposure,SpeedRatio,LocalSpeed_mps,PressureRatio,CrownAsymmetry,DominantDirection_deg,MaxProjectedArea_m2,WindRoseWeightedProjectedArea_m2,HorizontalGapProxy,RelativeSilhouetteDeficit,LoadPotential_m4_per_s2,MeanOccupiedUpwindPath_m,NeighbourComparisonComputed,NeighbourShelterDeltaWEI";
    for(int i=0;i<8;++i)out<<",Exposure_"<<i*45<<"deg";
    for(int i=0;i<8;++i)out<<",ProjectedArea_"<<i*45<<"deg_m2";
    out<<'\n';
    for(const auto& row:rows) {
        const auto& s=row.second;
        out<<quoted(row.first)<<','<<s.cwei<<','<<s.crownPressureExposure<<','<<s.meanWef<<','<<s.shelter<<','<<s.exposureP10<<','<<s.exposureP50<<','<<s.exposureP90<<','<<s.exposedFraction<<','<<s.upperExposure<<','<<s.lowerExposure<<','<<s.speedRatio<<','<<s.localSpeedMps<<','<<s.pressureRatio<<','<<s.crownAsymmetry<<','<<s.dominantDirectionDeg<<','<<s.maxProjectedAreaM2<<','<<s.weightedProjectedAreaM2<<','<<s.horizontalGapFraction<<','<<s.relativeSilhouetteDeficit<<','<<s.crownLoadPotential<<','<<s.meanOccupiedUpwindPathM<<','<<(s.neighbourComparisonComputed?"true":"false")<<',';
        if(s.neighbourComparisonComputed)out<<s.neighbourShelterDeltaWEI;
        for(double a:s.directionalExposure)out<<','<<a;
        for(double a:s.directionalProjectedAreaM2)out<<','<<a;
        out<<'\n';
    }
}
void exportPointsCSV(const std::string& path,const Geometry& g,const Results& res) {
    if(g.points.size()!=res.points.size())throw std::invalid_argument("point result count mismatch");
    std::ofstream out(path);if(!out)throw std::runtime_error("cannot create point CSV");
    out<<std::setprecision(10)<<"x,y,z,WEF,WSF,DWE,WEI,WPWE,WindExposureFrequency,UpwindOpenness,ShelterAngle_deg,ObstructionDistance_m,SpeedRatio,LocalSpeed_mps,LocalPressure_Pa,PressureRatio,OccupiedUpwindPath_m,OccupiedUpwindPathFraction";
    for(int i=0;i<8;++i)out<<",Exposure_"<<i*45<<"deg";
    out<<'\n';
    for(size_t i=0;i<g.points.size();++i) {
        const auto& p=g.points[i];const auto& r=res.points[i];
        out<<p.x<<','<<p.y<<','<<p.z<<','<<r.wef<<','<<r.wsf<<','<<r.dwe<<','<<r.wei<<','<<r.wpwe<<','<<r.wefreq<<','<<r.upwindOpenness<<','<<r.shelterAngleDeg<<','<<r.obstructionDistanceM<<','<<r.speedRatio<<','<<r.localSpeedMps<<','<<r.localPressurePa<<','<<r.pressureRatio<<','<<r.upwindOccupiedPathM<<','<<r.upwindPathOccupancy;
        for(double e:r.compassExposure) out<<','<<e;
        out<<'\n';
    }
}
void exportProfilesCSV(const std::string& path,const std::vector<std::pair<std::string, Summary>>& rows) {
    std::ofstream out(path);if(!out)throw std::runtime_error("cannot create profiles CSV");
    out<<std::setprecision(10)<<"entity,band,z_min_m,z_max_m,point_count,WEI,WPWE,SpeedRatio\n";
    for(const auto& row:rows)for(size_t i=0;i<row.second.verticalProfile.size();++i) {
        const auto& b=row.second.verticalProfile[i];
        out<<quoted(row.first)<<','<<i<<','<<b.zMin<<','<<b.zMax<<','<<b.count<<','<<b.wei<<','<<b.wpwe<<','<<b.speedRatio<<'\n';
    }
}
} // namespace ventus
