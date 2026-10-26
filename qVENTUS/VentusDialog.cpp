#include "VentusDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <stdexcept>

namespace {
QDoubleSpinBox* number(double value,double min,double max,int decimals=3) {
    auto* b=new QDoubleSpinBox;
    b->setRange(min,max);b->setDecimals(decimals);b->setValue(value);
    b->setSingleStep(decimals>=2?0.1:1.0);
    return b;
}
}
VentusDialog::VentusDialog(QWidget* parent):QDialog(parent) {
    setWindowTitle(tr("VENTUS — Wind Exposure and Shelter"));resize(575,660);
    auto* main=new QVBoxLayout(this);
    auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* content=new QWidget;auto* contentLayout=new QVBoxLayout(content);
    auto* wind=new QGroupBox(tr("Wind climate"));auto* wl=new QFormLayout(wind);
    m_source=new QComboBox; m_source->addItems({tr("Single direction"),tr("Wind rose CSV")});
    wl->addRow(tr("Input"),m_source);
    m_direction=number(270,0,360,1);m_speed=number(5,0,100,2);
    wl->addRow(tr("Wind FROM direction [°]"),m_direction);
    wl->addRow(tr("Reference wind speed [m/s]"),m_speed);
    auto* fileRow=new QWidget;auto* fileLayout=new QHBoxLayout(fileRow);fileLayout->setContentsMargins(0,0,0,0);
    m_rose=new QLineEdit;auto* browse=new QPushButton(tr("Browse..."));
    fileLayout->addWidget(m_rose,1);fileLayout->addWidget(browse);wl->addRow(tr("Wind rose CSV"),fileRow);
    m_rose->setPlaceholderText(tr("from_deg,frequency,speed_mps"));
    connect(browse,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getOpenFileName(this,tr("Open wind rose"),QString(),tr("CSV files (*.csv *.txt);;All files (*)"));
        if(!path.isEmpty())m_rose->setText(path);
    });
    connect(m_source,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int v){
        m_direction->setEnabled(v==0);m_speed->setEnabled(v==0);m_rose->setEnabled(v==1);
    });m_rose->setEnabled(false);
    contentLayout->addWidget(wind);

    auto* geometry=new QGroupBox(tr("Geometry and angular sampling (Level 1)"));auto* gl=new QFormLayout(geometry);
    m_context=new QComboBox;
    m_context->addItems({tr("Selected entities (mutual shelter)"),tr("Target only"),tr("All visible / loaded geometry")});
    gl->addRow(tr("Occlusion context"),m_context);
    m_voxel=number(.5,.001,100,3);m_range=number(50,.1,10000,1);
    m_azimuth=new QSpinBox;m_azimuth->setRange(4,360);m_azimuth->setValue(16);
    m_elevations=new QSpinBox;m_elevations->setRange(1,30);m_elevations->setValue(3);
    m_minElevation=number(0,-89,89,1);m_maxElevation=number(30,-89,89,1);
    m_spread=number(15,0,60,1);m_exposed=number(.7,0,1,2);
    gl->addRow(tr("Voxel size [m]"),m_voxel);
    gl->addRow(tr("Maximum search distance [m]"),m_range);
    gl->addRow(tr("All-direction azimuth samples"),m_azimuth);
    gl->addRow(tr("Elevation samples"),m_elevations);
    gl->addRow(tr("Minimum elevation [°]"),m_minElevation);
    gl->addRow(tr("Maximum elevation [°]"),m_maxElevation);
    gl->addRow(tr("Wind sector half-width [°]"),m_spread);
    gl->addRow(tr("Exposure threshold [0–1]"),m_exposed);
    m_neighbours=new QCheckBox(tr("Calculate shelter contributed by neighbouring geometry (extra pass)"));
    gl->addRow(m_neighbours);
    m_compass=new QCheckBox(tr("Output 8 directional scalar fields (N, NE, E, SE, S, SW, W, NW)"));
    m_compass->setChecked(false);gl->addRow(m_compass);
    contentLayout->addWidget(geometry);

    auto* empirical=new QGroupBox(tr("Empirical attenuation (Level 2 — NOT CFD)"));auto* el=new QFormLayout(empirical);
    m_empirical=new QCheckBox(tr("Calculate empirical local-speed and pressure estimates"));m_empirical->setChecked(true);
    m_attenuation=number(.15,0,10,3);m_density=number(1.225,.5,2.0,3);
    el->addRow(m_empirical);el->addRow(tr("Occupied-voxel attenuation [1/m]"),m_attenuation);
    el->addRow(tr("Air density [kg/m³]"),m_density);
    auto* notice=new QLabel(tr("Speed attenuation is exp(-k × occupied upwind path length). The coefficient k must be calibrated for the vegetation or geometry. No acceleration, turbulence or wake recovery is simulated."));
    notice->setWordWrap(true);el->addRow(notice);
    connect(m_empirical,&QCheckBox::toggled,m_attenuation,&QWidget::setEnabled);
    connect(m_empirical,&QCheckBox::toggled,m_density,&QWidget::setEnabled);
    contentLayout->addWidget(empirical);contentLayout->addStretch();
    scroll->setWidget(content);main->addWidget(scroll,1);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    main->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{
        try {const auto o=options();const auto w=windRose();ventus::validate(o,w);accept();}
        catch(const std::exception& e){QMessageBox::warning(this,tr("VENTUS: invalid input"),QString::fromUtf8(e.what()));}
    });
}
ventus::Options VentusDialog::options()const {
    ventus::Options o;
    o.voxelSize=m_voxel->value();o.maxDistance=m_range->value();o.azimuthSamples=m_azimuth->value();
    o.elevationSamples=m_elevations->value();o.minElevationDeg=m_minElevation->value();o.maxElevationDeg=m_maxElevation->value();
    o.sectorHalfWidthDeg=m_spread->value();o.exposedThreshold=m_exposed->value();o.crownThreshold=m_exposed->value();
    o.attenuationPerMeter=m_empirical->isChecked()?m_attenuation->value():0.;o.airDensity=m_density->value();
    return o;
}
std::vector<ventus::RoseBin> VentusDialog::windRose()const {
    return m_source->currentIndex()==0?std::vector<ventus::RoseBin>{{m_direction->value(),1,m_speed->value()}}:
        ventus::loadWindRoseCSV(m_rose->text().toStdString());
}
int VentusDialog::occlusionMode()const{return m_context->currentIndex();}
bool VentusDialog::compareNeighbours()const{return m_neighbours->isChecked();}
bool VentusDialog::writeCompass()const{return m_compass->isChecked();}
bool VentusDialog::empiricalEnabled()const{return m_empirical->isChecked();}
