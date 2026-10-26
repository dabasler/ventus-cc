#include "qVENTUS.h"
#include "VentusDialog.h"
#include "ventus/Engine.h"

#include <GenericIndexedMesh.h>
#include <ccGenericMesh.h>
#include <ccHObjectCaster.h>
#include <ccPointCloud.h>
#include <ccScalarField.h>
#include <ccMainAppInterface.h>
#include <ccCommandLineInterface.h>
#include <BinFilter.h>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMainWindow>
#include <QProgressDialog>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
constexpr auto META_KEY="qVENTUS/crownExposure/latest";
struct Entity {
    ccHObject* object=nullptr;
    ccPointCloud* cloud=nullptr;
    ccGenericMesh* mesh=nullptr;
};
Entity entityFor(ccHObject* e) {
    if(!e)return {};
    if(e->isKindOf(CC_TYPES::MESH)){
        auto* mesh=ccHObjectCaster::ToGenericMesh(e);
        auto* cloud=mesh?ccHObjectCaster::ToPointCloud(mesh->getAssociatedCloud()):nullptr;
        if(mesh&&cloud)return {e,cloud,mesh};
    }
    if(e->isA(CC_TYPES::POINT_CLOUD)) {
        auto* cloud=ccHObjectCaster::ToPointCloud(e);
        if(cloud)return {e,cloud,nullptr};
    }
    return {};
}
std::vector<Entity> uniqueEntities(const ccHObject::Container& objs) {
    std::vector<Entity> all;QSet<ccPointCloud*> seen;
    // Prefer meshes, not their associated/duplicate vertex clouds.
    for(int pass=0;pass<2;++pass)for(auto* obj:objs) {
        if(!obj || (pass==0)!=obj->isKindOf(CC_TYPES::MESH))continue;
        auto e=entityFor(obj);
        if(e.cloud&&!seen.contains(e.cloud)) {seen.insert(e.cloud);all.push_back(e);}
    }
    return all;
}
std::vector<Entity> visibleEntities(ccHObject* root) {
    if(!root)return {};
    ccHObject::Container objs, meshes, clouds;
    root->filterChildren(meshes,true,CC_TYPES::MESH);
    root->filterChildren(clouds,true,CC_TYPES::POINT_CLOUD);
    for(auto* o:meshes)if(o&&o->isDisplayed())objs.push_back(o);
    for(auto* o:clouds)if(o&&o->isDisplayed())objs.push_back(o);
    return uniqueEntities(objs);
}
ventus::Geometry geometryFor(const Entity& e) {
    ventus::Geometry result;
    if(!e.cloud)return result;
    result.points.reserve(e.cloud->size());
    for(unsigned i=0;i<e.cloud->size();++i){
        const auto* p=e.cloud->getPoint(i);
        result.points.push_back({static_cast<double>(p->x),static_cast<double>(p->y),static_cast<double>(p->z)});
    }
    if(e.mesh) {
        auto* indexed=dynamic_cast<CCCoreLib::GenericIndexedMesh*>(e.mesh);
        if(!indexed)throw std::runtime_error("mesh must be indexed for VENTUS");
        result.triangles.reserve(indexed->size());
        indexed->placeIteratorAtBeginning();
        for(unsigned i=0;i<indexed->size();++i) {
            auto* t=indexed->getNextTriangleVertIndexes();
            if(t)result.triangles.push_back({t->i1,t->i2,t->i3});
        }
    }
    return result;
}
QJsonArray numbers(const std::array<double,8>& v) {
    QJsonArray arr;for(double x:v)arr.append(x);return arr;
}
QJsonObject jsonSummary(const ventus::Summary& s,const ventus::Options& opt,
                        const std::vector<ventus::RoseBin>& rose,const Entity& entity,
                        const QString& context,bool empirical,double neighbourDelta) {
    QJsonObject j;
    j["schemaVersion"]=1;j["plugin"]="qVENTUS";j["displayName"]="VENTUS";
    j["version"]="0.2.0-dev";j["createdUtc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    j["runId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    j["entityName"]=entity.object->getName();j["context"]=context;
    j["cwei"]=s.cwei;j["crownPressureExposure"]=s.crownPressureExposure;
    j["meanWef"]=s.meanWef;j["shelter"]=s.shelter;
    j["p10"]=s.exposureP10;j["p50"]=s.exposureP50;j["p90"]=s.exposureP90;
    j["exposedFraction"]=s.exposedFraction;j["upperExposure"]=s.upperExposure;j["lowerExposure"]=s.lowerExposure;
    j["speedRatio"]=s.speedRatio;j["localSpeedMps"]=s.localSpeedMps;j["pressureRatio"]=s.pressureRatio;
    j["crownAsymmetry"]=s.crownAsymmetry;j["dominantDirectionDeg"]=s.dominantDirectionDeg;
    j["maxProjectedAreaM2"]=s.maxProjectedAreaM2;
    j["weightedProjectedAreaM2"]=s.weightedProjectedAreaM2;
    j["directionalExposure"]=numbers(s.directionalExposure);
    j["directionalProjectedAreaM2"]=numbers(s.directionalProjectedAreaM2);
    j["relativeSilhouetteDeficit"]=s.relativeSilhouetteDeficit;
    j["crownLoadPotential"]=s.crownLoadPotential;
    j["meanOccupiedUpwindPathM"]=s.meanOccupiedUpwindPathM;
    j["neighbourShelterDeltaWEI"]=neighbourDelta;
    j["neighbourComparisonComputed"]=s.neighbourComparisonComputed;
    j["voxelSizeM"]=opt.voxelSize;j["rangeM"]=opt.maxDistance;
    j["attenuationPerMeter"]=opt.attenuationPerMeter;j["empiricalEnabled"]=empirical;
    j["azimuthSamples"]=opt.azimuthSamples;j["elevationSamples"]=opt.elevationSamples;
    j["angleMinDeg"]=opt.minElevationDeg;j["angleMaxDeg"]=opt.maxElevationDeg;
    j["sectorHalfWidthDeg"]=opt.sectorHalfWidthDeg;
    j["crownThreshold"]=opt.crownThreshold;
    j["geometryUnits"]="meters";
    j["model"]="occupied-voxel ray visibility + uncalibrated exponential attenuation; no CFD";
    QJsonArray winds;for(auto w:rose){QJsonObject v;v["fromDeg"]=w.fromDeg;v["frequency"]=w.frequency;v["speedMps"]=w.speed;winds.append(v);}j["windRose"]=winds;
    QJsonArray bands;for(const auto& b:s.verticalProfile){QJsonObject x;
        x["zMin"]=b.zMin;x["zMax"]=b.zMax;x["count"]=static_cast<int>(b.count);
        x["wei"]=b.wei;x["wpwe"]=b.wpwe;x["speedRatio"]=b.speedRatio;bands.append(x);}
    j["verticalProfile"]=bands;
    return j;
}
ventus::Summary summaryFromJson(const QJsonObject& j) {
    ventus::Summary s;
    s.cwei=j.value("cwei").toDouble();s.crownPressureExposure=j.value("crownPressureExposure").toDouble();
    s.meanWef=j.value("meanWef").toDouble();s.shelter=j.value("shelter").toDouble();
    s.exposureP10=j.value("p10").toDouble();s.exposureP50=j.value("p50").toDouble();s.exposureP90=j.value("p90").toDouble();
    s.exposedFraction=j.value("exposedFraction").toDouble();s.upperExposure=j.value("upperExposure").toDouble();
    s.lowerExposure=j.value("lowerExposure").toDouble();s.speedRatio=j.value("speedRatio").toDouble();
    s.localSpeedMps=j.value("localSpeedMps").toDouble();s.pressureRatio=j.value("pressureRatio").toDouble();
    s.crownAsymmetry=j.value("crownAsymmetry").toDouble();s.dominantDirectionDeg=j.value("dominantDirectionDeg").toDouble();
    s.maxProjectedAreaM2=j.value("maxProjectedAreaM2").toDouble();s.weightedProjectedAreaM2=j.value("weightedProjectedAreaM2").toDouble();
    s.horizontalGapFraction=s.meanWef;s.relativeSilhouetteDeficit=j.value("relativeSilhouetteDeficit").toDouble();
    s.crownLoadPotential=j.value("crownLoadPotential").toDouble();
    s.meanOccupiedUpwindPathM=j.value("meanOccupiedUpwindPathM").toDouble();
    s.neighbourComparisonComputed=j.value("neighbourComparisonComputed").toBool();
    s.neighbourShelterDeltaWEI=j.value("neighbourShelterDeltaWEI").toDouble();
    const auto dir=j.value("directionalExposure").toArray(),area=j.value("directionalProjectedAreaM2").toArray();
    for(int k=0;k<8;++k){s.directionalExposure[k]=dir.at(k).toDouble();s.directionalProjectedAreaM2[k]=area.at(k).toDouble();}
    const auto bands=j.value("verticalProfile").toArray();
    for(const auto& v:bands){const auto b=v.toObject();ventus::ProfileBand band;
        band.zMin=b.value("zMin").toDouble();band.zMax=b.value("zMax").toDouble();
        band.count=static_cast<size_t>(b.value("count").toInt());
        band.wei=b.value("wei").toDouble();band.wpwe=b.value("wpwe").toDouble();
        band.speedRatio=b.value("speedRatio").toDouble();s.verticalProfile.push_back(band);
    }
    return s;
}
QString uniqueFieldName(ccPointCloud* cloud,QString base) {
    if(cloud->getScalarFieldIndexByName(qPrintable(base))<0)return base;
    for(int i=2;i<100000;++i){auto n=QStringLiteral("%1 #%2").arg(base).arg(i);
        if(cloud->getScalarFieldIndexByName(qPrintable(n))<0)return n;
    }
    return base+QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool field(ccPointCloud* cloud,const QString& name,size_t count,const std::function<double(size_t)>& get) {
    if(!cloud||cloud->size()!=count)return false;
    const int idx=cloud->addScalarField(qPrintable(uniqueFieldName(cloud,name)));
    if(idx<0)return false;
    // CloudCompare 2.14 returns a shared_ptr, not a raw scalar-field pointer.
    auto sf=cloud->getScalarField(idx);
    if(!sf){cloud->deleteScalarField(idx);return false;}
    for(size_t i=0;i<count;++i)sf->setValue(static_cast<unsigned>(i),static_cast<ScalarType>(get(i)));
    sf->computeMinAndMax();cloud->setCurrentDisplayedScalarField(idx);cloud->showSF(true);
    return true;
}
void outputFields(const Entity& t,const ventus::Results& result,
                  const ventus::Results* alone,bool compass,bool empirical) {
    auto* cloud=t.cloud;const size_t n=result.points.size();
    using R=ventus::PointResult;
    auto f=[&](const char* name,double R::* member) {
        if(!field(cloud,QString::fromUtf8(name),n,[&](size_t i){return result.points[i].*member;}))
            throw std::runtime_error(std::string("cannot allocate scalar field: ")+name);
    };
    f("VENTUS WEF [0-1]",&R::wef);
    f("VENTUS WSF [0-1]",&R::wsf);
    f("VENTUS DWE [0-1]",&R::dwe);
    f("VENTUS WEI [0-1]",&R::wei);
    f("VENTUS WPWE [0-1]",&R::wpwe);
    f("VENTUS Wind Exposure Frequency [0-1]",&R::wefreq);
    f("VENTUS Upwind Openness [0-1]",&R::upwindOpenness);
    f("VENTUS Shelter Angle [deg]",&R::shelterAngleDeg);
    f("VENTUS Obstruction Distance [m]",&R::obstructionDistanceM);
    f("VENTUS Upwind Occupied Path [m]",&R::upwindOccupiedPathM);
    f("VENTUS Upwind Path Occupancy [0-1]",&R::upwindPathOccupancy);
    if(empirical) {
        f("VENTUS Empirical Speed Ratio [0-1]",&R::speedRatio);
        f("VENTUS Empirical Local Speed [m/s]",&R::localSpeedMps);
        f("VENTUS Empirical Local Pressure [Pa]",&R::localPressurePa);
        f("VENTUS Empirical Pressure Ratio [0-1]",&R::pressureRatio);
    }
    if(compass)for(int i=0;i<8;++i) {
        const QString name=QStringLiteral("VENTUS Exposure %1deg [0-1]").arg(i*45);
        if(!field(cloud,name,n,[&](size_t j){return result.points[j].compassExposure[i];}))
            throw std::runtime_error("cannot allocate compass scalar field");
    }
    if(alone) {
        if(!field(cloud,QStringLiteral("VENTUS Neighbour Shelter Delta WEI"),n,[&](size_t i){
            return alone->points[i].wei-result.points[i].wei;
        }))throw std::runtime_error("cannot allocate neighbour shelter scalar field");
    }
    // Present WEI as the default displayed field, not the last computed field.
    for(int i=cloud->getNumberOfScalarFields()-1;i>=0;--i) {
        const auto sf=cloud->getScalarField(i);
        if(sf&&QString::fromUtf8(sf->getName()).startsWith(QStringLiteral("VENTUS WEI [0-1]"))) {
            cloud->setCurrentDisplayedScalarField(i);break;
        }
    }
    cloud->showSF(true);t.object->showSF(true);t.object->prepareDisplayForRefresh_recursive();
}
} // namespace

qVENTUS::qVENTUS(QObject* parent):QObject(parent),ccStdPluginInterface(":/CC/plugin/qVENTUS/info.json") {}
void qVENTUS::onNewSelection(const ccHObject::Container& selected) {
    auto targets=uniqueEntities(selected);
    const bool good=!targets.empty();bool canExport=false;
    for(const auto& e:targets)if(e.cloud->metaData().contains(QString::fromUtf8(META_KEY)))canExport=true;
    if(m_calculate)m_calculate->setEnabled(good);
    if(m_cleanup)m_cleanup->setEnabled(good);
    if(m_export)m_export->setEnabled(canExport);
}
QList<QAction*> qVENTUS::getActions() {
    if(!m_calculate){m_calculate=new QAction(tr("VENTUS Calculate Wind Exposure..."),this);
        m_calculate->setToolTip(getDescription());m_calculate->setIcon(getIcon());
        connect(m_calculate,&QAction::triggered,this,&qVENTUS::calculateAction);}
    if(!m_export){m_export=new QAction(tr("VENTUS Export Crown Metrics..."),this);
        connect(m_export,&QAction::triggered,this,&qVENTUS::exportAction);}
    if(!m_cleanup){m_cleanup=new QAction(tr("VENTUS Cleanup Results..."),this);
        connect(m_cleanup,&QAction::triggered,this,&qVENTUS::cleanupAction);}
    if(m_app)onNewSelection(m_app->getSelectedEntities());
    return {m_calculate,m_export,m_cleanup};
}
// CloudCompare's CLI uses a flat, ordered list of currently loaded point clouds
// and meshes.  No QMainWindow or selection/database root exists in this mode.
namespace {
struct VentusCLIConfig {
    QString targetName;
    int targetIndex = -1;
    QString context = "TARGET";  // TARGET, ALL, NAMED
    QStringList contextNames;
    QString rosePath, crownCsv, pointsCsv, binPath;
    ventus::Options options;
    double direction = 270.0, speed = 5.0;
    bool empirical = true, compass = false, compareNeighbours = false;
    bool listOnly = false, overwrite = false, helpOnly = false;
    bool explicitlySetSpeed = false, explicitlySetDirection = false;
};

// Coordinates from distinct CloudCompare files can have distinct global
// shifts/scales. Normalize context to the TARGET's local coordinates before
// building the voxel grid. All point and context geometry must share a frame.
ventus::Geometry geometryInTargetFrame(const Entity& source, const ccPointCloud* targetCloud) {
    ventus::Geometry g = geometryFor(source);
    const auto& sourceShift = source.cloud->getGlobalShift();
    const auto& targetShift = targetCloud->getGlobalShift();
    const double fromScale = source.cloud->getGlobalScale();
    const double toScale = targetCloud->getGlobalScale();
    if (!std::isfinite(fromScale) || !std::isfinite(toScale) || fromScale <= 0.0 || toScale <= 0.0)
        throw std::runtime_error("invalid CloudCompare global coordinate scale");
    // Preserve the target's exact local coordinates to avoid rounding changes.
    if (source.cloud == targetCloud) return g;
    for (auto& p : g.points) {
        p.x = ((p.x / fromScale - sourceShift.x) + targetShift.x) * toScale;
        p.y = ((p.y / fromScale - sourceShift.y) + targetShift.y) * toScale;
        p.z = ((p.z / fromScale - sourceShift.z) + targetShift.z) * toScale;
    }
    return g;
}

// Normalizing geometry into the target coordinate frame also changes units if
// the target's global scale is not 1. Core distance parameters use metres:
// reject non-unit target scale rather than silently producing wrong results.
void requireMeterScale(const Entity& target) {
    if (std::abs(target.cloud->getGlobalScale() - 1.0) > 1e-10)
        throw std::runtime_error("target global scale is not 1.0; VENTUS CLI expects metre-scale local coordinates");
}

std::vector<Entity> commandEntities(ccCommandLineInterface& cmd) {
    std::vector<Entity> out;
    QSet<ccPointCloud*> seen;
    // Mesh first: do not analyze a mesh's vertex cloud twice.
    for (const auto& desc : cmd.meshes()) {
        Entity e = entityFor(desc.mesh);
        if (e.cloud && !seen.contains(e.cloud)) {seen.insert(e.cloud);out.push_back(e);}
    }
    for (const auto& desc : cmd.clouds()) {
        Entity e = entityFor(desc.pc);
        if (e.cloud && !seen.contains(e.cloud)) {seen.insert(e.cloud);out.push_back(e);}
    }
    return out;
}

class CommandVentus final : public ccCommandLineInterface::Command {
public:
    CommandVentus() : ccCommandLineInterface::Command("VENTUS 3D Wind Exposure and Shelter Analysis", "VENTUS") {}

    bool process(ccCommandLineInterface& cmd) override {
        VentusCLIConfig c;
        QStringList& args = cmd.arguments();
        auto fail = [&](const QString& why) -> bool {return cmd.error(QStringLiteral("[VENTUS] ") + why);};
        auto next = [&](const QString& /*key*/, QString& value) -> bool {
            if (args.isEmpty() || args.front().startsWith('-'))
                return false;
            value = args.takeFirst();
            return !value.isEmpty();
        };
        auto numeric = [&](const QString& key, double& value) -> bool {
            QString s;
            if (!next(key,s)) return false;
            bool ok = false;
            value = s.toDouble(&ok);
            return ok && std::isfinite(value);
        };
        auto integer = [&](const QString& key, int& value) -> bool {
            QString s;
            if (!next(key,s)) return false;
            bool ok = false;
            value = s.toInt(&ok);
            return ok;
        };
        // Consume ONLY known VENTUS options. The first unknown token belongs
        // to CloudCompare's next command (e.g. -SAVE_CLOUDS).
        while (!args.isEmpty()) {
            const QString key = args.front().toUpper();
            if (key == "-TARGET") {
                args.pop_front();
                if (!next(key,c.targetName)) return fail("-TARGET requires one quoted entity name");
            } else if (key == "-TARGET_INDEX") {
                args.pop_front();
                if (!integer(key,c.targetIndex) || c.targetIndex < 0) return fail("-TARGET_INDEX requires nonnegative index");
            } else if (key == "-CONTEXT") {
                args.pop_front();
                if (!next(key,c.context)) return fail("-CONTEXT requires TARGET, ALL, or NAMED");
                c.context = c.context.toUpper();
                if (c.context != "TARGET" && c.context != "ALL" && c.context != "NAMED")
                    return fail("-CONTEXT must be TARGET, ALL, or NAMED");
            } else if (key == "-CONTEXT_NAMES") {
                args.pop_front();
                QString names;
                if (!next(key,names)) return fail("-CONTEXT_NAMES requires a comma-separated name list");
                c.contextNames = names.split(',',Qt::SkipEmptyParts);
                for (QString& name : c.contextNames) name = name.trimmed();
            } else if (key == "-DIRECTION") {
                args.pop_front();
                if (!numeric(key,c.direction)) return fail("-DIRECTION requires a finite number");
                c.explicitlySetDirection = true;
            } else if (key == "-SPEED") {
                args.pop_front();
                if (!numeric(key,c.speed)) return fail("-SPEED requires a finite number");
                c.explicitlySetSpeed = true;
            } else if (key == "-VOXEL") {
                args.pop_front();
                if (!numeric(key,c.options.voxelSize)) return fail("-VOXEL requires a finite number");
            } else if (key == "-RANGE") {
                args.pop_front();
                if (!numeric(key,c.options.maxDistance)) return fail("-RANGE requires a finite number");
            } else if (key == "-ATTENUATION") {
                args.pop_front();
                if (!numeric(key,c.options.attenuationPerMeter)) return fail("-ATTENUATION requires a finite number");
            } else if (key == "-AZIMUTH_SAMPLES") {
                args.pop_front();
                if (!integer(key,c.options.azimuthSamples)) return fail("-AZIMUTH_SAMPLES requires integer");
            } else if (key == "-ELEVATION_SAMPLES") {
                args.pop_front();
                if (!integer(key,c.options.elevationSamples)) return fail("-ELEVATION_SAMPLES requires integer");
            } else if (key == "-MIN_ELEVATION") {
                args.pop_front();
                if (!numeric(key,c.options.minElevationDeg)) return fail("-MIN_ELEVATION requires a number");
            } else if (key == "-MAX_ELEVATION") {
                args.pop_front();
                if (!numeric(key,c.options.maxElevationDeg)) return fail("-MAX_ELEVATION requires a number");
            } else if (key == "-SECTOR_HALF_WIDTH") {
                args.pop_front();
                if (!numeric(key,c.options.sectorHalfWidthDeg)) return fail("-SECTOR_HALF_WIDTH requires a number");
            } else if (key == "-ROSE") {
                args.pop_front();
                if (!next(key,c.rosePath)) return fail("-ROSE requires a CSV filename");
            } else if (key == "-EMPIRICAL") {
                args.pop_front();
                QString v;
                if (!next(key,v) || (v.toUpper() != "ON" && v.toUpper() != "OFF"))
                    return fail("-EMPIRICAL requires ON or OFF");
                c.empirical = (v.toUpper() == "ON");
            } else if (key == "-COMPASS") {
                args.pop_front();
                c.compass = true;
            } else if (key == "-COMPARE_NEIGHBOURS") {
                args.pop_front();
                c.compareNeighbours = true;
            } else if (key == "-EXPORT_CSV") {
                args.pop_front();
                if (!next(key,c.crownCsv)) return fail("-EXPORT_CSV requires an output filename");
            } else if (key == "-EXPORT_POINTS") {
                args.pop_front();
                if (!next(key,c.pointsCsv)) return fail("-EXPORT_POINTS requires an output filename");
            } else if (key == "-EXPORT_BIN") {
                args.pop_front();
                if (!next(key,c.binPath)) return fail("-EXPORT_BIN requires an output .bin filename");
            } else if (key == "-HELP") {
                args.pop_front();
                c.helpOnly = true;
            } else if (key == "-LIST") {
                args.pop_front();
                c.listOnly = true;
            } else if (key == "-OVERWRITE") {
                args.pop_front();
                c.overwrite = true;
            } else break;
        }
        if (c.helpOnly) {
            cmd.print("VENTUS CLI: -O file.bin -VENTUS -TARGET name [-CONTEXT TARGET|ALL|NAMED] ...");
            cmd.print("Options: -TARGET_INDEX i -LIST -CONTEXT_NAMES a,b -DIRECTION deg -SPEED m/s");
            cmd.print("-VOXEL m -RANGE m -ATTENUATION 1/m -ROSE wind.csv");
            cmd.print("-AZIMUTH_SAMPLES N -ELEVATION_SAMPLES N -MIN_ELEVATION deg -MAX_ELEVATION deg");
            cmd.print("-SECTOR_HALF_WIDTH deg -EMPIRICAL ON|OFF -COMPASS -COMPARE_NEIGHBOURS");
            cmd.print("-EXPORT_CSV crown.csv -EXPORT_POINTS points.csv -EXPORT_BIN result.bin -OVERWRITE");
            return true;
        }
        const auto loaded = commandEntities(cmd);
        if (c.listOnly) {
            for (size_t i = 0; i < loaded.size(); ++i)
                cmd.print(QStringLiteral("[VENTUS] entity %1: %2 (%3 points)")
                              .arg(i).arg(loaded[i].object->getName()).arg(loaded[i].cloud->size()));
            return true;
        }
        if (loaded.empty()) return fail("No clouds/meshes loaded. Use -O file.bin before -VENTUS.");
        if (c.targetIndex >= 0 && !c.targetName.isEmpty()) return fail("Use -TARGET or -TARGET_INDEX, not both");
        if (c.context == "NAMED" && c.contextNames.isEmpty()) return fail("-CONTEXT NAMED requires -CONTEXT_NAMES");
        if (!c.contextNames.isEmpty() && c.context != "NAMED") return fail("-CONTEXT_NAMES requires -CONTEXT NAMED");
        if (c.compareNeighbours && c.context == "TARGET") return fail("-COMPARE_NEIGHBOURS needs ALL or NAMED context");
        if (!c.rosePath.isEmpty() && (c.explicitlySetDirection || c.explicitlySetSpeed))
            return fail("-ROSE cannot be combined with -DIRECTION / -SPEED");
        if (c.speed < 0.0) return fail("Wind speed must be nonnegative");
        const Entity* target = nullptr;
        if (c.targetIndex >= 0) {
            if (static_cast<size_t>(c.targetIndex) >= loaded.size()) return fail("-TARGET_INDEX out of range (use -VENTUS -LIST)");
            target = &loaded[static_cast<size_t>(c.targetIndex)];
        } else if (!c.targetName.isEmpty()) {
            for (const Entity& e : loaded) if (e.object->getName() == c.targetName) {
                if (target) return fail("Duplicate target name: use -VENTUS -LIST and -TARGET_INDEX");
                target = &e;
            }
            if (!target) return fail(QStringLiteral("Target not found: '%1'. Use -VENTUS -LIST.").arg(c.targetName));
        } else if (loaded.size() == 1) target = &loaded[0];
        else return fail("Multiple entities loaded. Specify -TARGET or -TARGET_INDEX (see -VENTUS -LIST).");

        // Prevent accidentally overwriting source files, or output files from
        // an earlier regression. Explicit -OVERWRITE opts in to replacement.
        QStringList outputPaths;
        if (!c.crownCsv.isEmpty()) {
            if (!c.crownCsv.endsWith(".csv",Qt::CaseInsensitive)) return fail("-EXPORT_CSV path must end in .csv");
            outputPaths << c.crownCsv << (c.crownCsv.left(c.crownCsv.size()-4) + "-profiles.csv");
        }
        if (!c.pointsCsv.isEmpty()) outputPaths << c.pointsCsv;
        if (!c.binPath.isEmpty()) {
            if (!c.binPath.endsWith(".bin",Qt::CaseInsensitive)) return fail("-EXPORT_BIN path must end in .bin");
            outputPaths << c.binPath;
        }
        QSet<QString> distinctPaths;
        for (const auto& path : outputPaths) {
            if (distinctPaths.contains(QFileInfo(path).absoluteFilePath()))
                return fail("Output paths must be distinct");
            distinctPaths.insert(QFileInfo(path).absoluteFilePath());
        }
        for (const auto& path : outputPaths) {
            const QFileInfo info(path);
            if (!info.dir().exists()) return fail(QStringLiteral("Output directory does not exist: %1").arg(info.dir().path()));
            if (info.exists() && !c.overwrite) return fail(QStringLiteral("Output exists: %1 (use -OVERWRITE)").arg(path));
        }

        try {
            requireMeterScale(*target);
            const auto rose = c.rosePath.isEmpty()
                ? std::vector<ventus::RoseBin>{{c.direction,1.0,c.speed}}
                : ventus::loadWindRoseCSV(c.rosePath.toStdString());
            ventus::validate(c.options,rose);
            const auto targetGeometry = geometryInTargetFrame(*target,target->cloud);
            if (targetGeometry.points.empty()) return fail("Selected target cloud is empty");
            ventus::VoxelGrid voxels(c.options.voxelSize);
            voxels.add(targetGeometry); // always include the target for self-occlusion
            QSet<ccPointCloud*> included;
            included.insert(target->cloud);
            if (c.context != "TARGET") {
                for (const auto& e : loaded) {
                    if (included.contains(e.cloud)) continue;
                    if (c.context == "NAMED" && !c.contextNames.contains(e.object->getName())) continue;
                    voxels.add(geometryInTargetFrame(e,target->cloud));
                    included.insert(e.cloud);
                }
                if (c.context == "NAMED") for (const auto& name : c.contextNames) {
                    bool found = false;
                    for (const auto& e : loaded) if (e.object->getName() == name) found = true;
                    if (!found) return fail(QStringLiteral("Context entity not found: %1").arg(name));
                }
            }
            const auto weights = targetGeometry.triangles.empty()
                ? ventus::equalVoxelWeights(targetGeometry,c.options.voxelSize)
                : ventus::meshVertexAreaWeights(targetGeometry);
            auto result = ventus::calculate(targetGeometry,voxels,rose,c.options,weights);
            ventus::Results alone;
            double delta = 0.0;
            if (c.compareNeighbours) {
                ventus::VoxelGrid self(c.options.voxelSize);
                self.add(targetGeometry);
                alone = ventus::calculate(targetGeometry,self,rose,c.options,weights);
                delta = alone.crown.cwei - result.crown.cwei;
                result.crown.neighbourComparisonComputed = true;
                result.crown.neighbourShelterDeltaWEI = delta;
            }
            outputFields(*target,result,c.compareNeighbours?&alone:nullptr,c.compass,c.empirical);
            const auto meta = jsonSummary(result.crown,c.options,rose,*target,c.context,c.empirical,delta);
            target->cloud->setMetaData(QString::fromUtf8(META_KEY),
                QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)));
            if (!c.crownCsv.isEmpty()) {
                const auto row = std::vector<std::pair<std::string,ventus::Summary>>{
                    {target->object->getName().toStdString(),result.crown}};
                ventus::exportSummaryCSV(c.crownCsv.toStdString(),row);
                QString profilesPath = c.crownCsv.left(c.crownCsv.size() - 4);
                profilesPath += QStringLiteral("-profiles.csv");
                ventus::exportProfilesCSV(profilesPath.toStdString(),row);
            }
            if (!c.pointsCsv.isEmpty())
                ventus::exportPointsCSV(c.pointsCsv.toStdString(),targetGeometry,result);
            if (!c.binPath.isEmpty()) {
                // Save exactly the target entity, including scalar fields and
                // persistent crown metadata, using CloudCompare's native BIN.
                BinFilter filter;
                FileIOFilter::SaveParameters save;
                save.alwaysDisplaySaveDialog = false;
                if (filter.saveToFile(target->object,c.binPath,save) != CC_FERR_NO_ERROR)
                    return fail(QStringLiteral("Could not save BIN: %1").arg(c.binPath));
            }
            cmd.print(QStringLiteral("[VENTUS] %1: CWEI=%2 WPWE=%3 pressure ratio=%4; %5 context entities")
                .arg(target->object->getName())
                .arg(result.crown.cwei,0,'g',6)
                .arg(result.crown.crownPressureExposure,0,'g',6)
                .arg(result.crown.pressureRatio,0,'g',6)
                .arg(included.size()));
            if (outputPaths.isEmpty())
                cmd.warning("[VENTUS] No outputs requested: use -EXPORT_BIN / -EXPORT_CSV, or CloudCompare -SAVE_CLOUDS.");
            return true;
        } catch (const std::exception& e) {
            return fail(QString::fromUtf8(e.what()));
        }
    }
};
} // namespace

void qVENTUS::registerCommands(ccCommandLineInterface* cmd) {
    if (cmd) cmd->registerCommand(ccCommandLineInterface::Command::Shared(new CommandVentus));
}
void qVENTUS::calculateAction() {
    if(!m_app)return;
    auto targets=uniqueEntities(m_app->getSelectedEntities());if(targets.empty())return;
    VentusDialog dlg(m_app->getMainWindow());if(dlg.exec()!=QDialog::Accepted)return;
    const auto opt=dlg.options();
    const auto rose=dlg.windRose();
    try {
        std::vector<Entity> context;
        QString contextLabel;
        if(dlg.occlusionMode()==0){context=targets;contextLabel="selected entities";}
        else if(dlg.occlusionMode()==1){contextLabel="target only";}
        else{context=visibleEntities(m_app->dbRootObject());contextLabel="all visible geometry";}
        // Cache extracted geometry so selected context is not reread per target.
        struct Cached {ccPointCloud* cloud;ventus::Geometry geometry;};
        std::vector<Cached> cached;
        auto extract=[&](const Entity& e)->const ventus::Geometry& {
            for(const auto& x:cached)if(x.cloud==e.cloud)return x.geometry;
            cached.push_back({e.cloud,geometryFor(e)});return cached.back().geometry;
        };
        // Building the cache up-front also makes references stable.
        for(const auto& e:targets)extract(e);
        for(const auto& e:context)extract(e);
        for(const auto& target:targets) {
            const auto& geom=extract(target);
            if(geom.points.empty())continue;
            ventus::VoxelGrid grid(opt.voxelSize);
            if(dlg.occlusionMode()==1)grid.add(geom);
            else {
                QSet<ccPointCloud*> visited;
                for(const auto& e:context)if(!visited.contains(e.cloud)) {
                    visited.insert(e.cloud);grid.add(extract(e));
                }
                if(!visited.contains(target.cloud))grid.add(geom);
            }
            QProgressDialog progress(tr("VENTUS: %1").arg(target.object->getName()),tr("Cancel"),0,static_cast<int>(geom.points.size()),m_app->getMainWindow());
            progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);
            auto callback=[&](size_t done,size_t total)->bool {
                progress.setMaximum(static_cast<int>(total));progress.setValue(static_cast<int>(done));
                QApplication::processEvents();return !progress.wasCanceled();
            };
            std::vector<double> weights=geom.triangles.empty()?ventus::equalVoxelWeights(geom,opt.voxelSize):ventus::meshVertexAreaWeights(geom);
            auto res=ventus::calculate(geom,grid,rose,opt,weights,callback);
            ventus::Results alone;bool compared=dlg.compareNeighbours()&&dlg.occlusionMode()!=1;
            double neighbourDelta=0;
            if(compared) {
                ventus::VoxelGrid self(opt.voxelSize);self.add(geom);
                alone=ventus::calculate(geom,self,rose,opt,weights,callback);
                neighbourDelta=alone.crown.cwei-res.crown.cwei;
                res.crown.neighbourComparisonComputed=true;
                res.crown.neighbourShelterDeltaWEI=neighbourDelta;
            }
            progress.close();
            outputFields(target,res,compared?&alone:nullptr,dlg.writeCompass(),dlg.empiricalEnabled());
            const auto json=jsonSummary(res.crown,opt,rose,target,contextLabel,dlg.empiricalEnabled(),neighbourDelta);
            target.cloud->setMetaData(QString::fromUtf8(META_KEY),QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
            m_app->dispToConsole(QStringLiteral("VENTUS: %1: CWEI=%2, WPWE=%3, empirical speed ratio=%4")
                .arg(target.object->getName()).arg(res.crown.cwei,0,'f',3)
                .arg(res.crown.crownPressureExposure,0,'f',3).arg(res.crown.speedRatio,0,'f',3),
                ccMainAppInterface::STD_CONSOLE_MESSAGE);
        }
        m_app->updateUI();m_app->refreshAll();onNewSelection(m_app->getSelectedEntities());
    } catch (const std::exception& e) {
        QMessageBox::critical(m_app->getMainWindow(),tr("VENTUS calculation failed"),QString::fromUtf8(e.what()));
    }
}
void qVENTUS::exportAction() {
    if(!m_app)return;
    auto targets=uniqueEntities(m_app->getSelectedEntities());
    std::vector<std::pair<std::string,ventus::Summary>> rows;
    for(const auto& target:targets){
        const auto& data=target.cloud->metaData();auto it=data.constFind(QString::fromUtf8(META_KEY));
        if(it==data.constEnd())continue;
        auto doc=QJsonDocument::fromJson(it.value().toString().toUtf8());if(!doc.isObject())continue;
        rows.push_back({target.object->getName().toStdString(),summaryFromJson(doc.object())});
    }
    if(rows.empty()){QMessageBox::information(m_app->getMainWindow(),tr("VENTUS"),tr("No VENTUS crown metrics are stored on the selected entities."));return;}
    auto path=QFileDialog::getSaveFileName(m_app->getMainWindow(),tr("Export VENTUS crown metrics"),QStringLiteral("ventus-crown.csv"),tr("CSV (*.csv)"));
    if(path.isEmpty())return;
    if(!path.endsWith(QStringLiteral(".csv"),Qt::CaseInsensitive))path+=".csv";
    try {
        ventus::exportSummaryCSV(path.toStdString(),rows);
        const QString profiles=path.left(path.size()-4)+QStringLiteral("-profiles.csv");
        ventus::exportProfilesCSV(profiles.toStdString(),rows);
        QMessageBox::information(m_app->getMainWindow(),tr("VENTUS"),tr("Saved crown summary and height-profile CSV files."));
    }catch(const std::exception& e){QMessageBox::critical(m_app->getMainWindow(),tr("VENTUS export failed"),QString::fromUtf8(e.what()));}
}
void qVENTUS::cleanupAction() {
    if(!m_app)return;
    auto targets=uniqueEntities(m_app->getSelectedEntities());if(targets.empty())return;
    if(QMessageBox::question(m_app->getMainWindow(),tr("VENTUS cleanup"),
        tr("Remove VENTUS scalar fields and stored crown metrics from selected objects?"))!=QMessageBox::Yes)return;
    for(const auto& e:targets){
        for(int i=e.cloud->getNumberOfScalarFields()-1;i>=0;--i){
            const auto sf=e.cloud->getScalarField(i);
            if(sf&&QString::fromUtf8(sf->getName()).startsWith(QStringLiteral("VENTUS ")))
                e.cloud->deleteScalarField(i);
        }
        e.cloud->removeMetaData(QString::fromUtf8(META_KEY));e.object->prepareDisplayForRefresh_recursive();
    }
    m_app->updateUI();m_app->refreshAll();onNewSelection(m_app->getSelectedEntities());
}
