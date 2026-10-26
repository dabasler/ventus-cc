#pragma once
#include <QDialog>
#include "ventus/Engine.h"
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;

class VentusDialog : public QDialog {
public:
    explicit VentusDialog(QWidget* parent=nullptr);
    ventus::Options options() const;
    std::vector<ventus::RoseBin> windRose() const;
    int occlusionMode() const;
    bool compareNeighbours() const;
    bool writeCompass() const;
    bool empiricalEnabled() const;
private:
    QDoubleSpinBox* m_direction=nullptr;
    QDoubleSpinBox* m_speed=nullptr;
    QLineEdit* m_rose=nullptr;
    QComboBox* m_source=nullptr;
    QComboBox* m_context=nullptr;
    QDoubleSpinBox* m_voxel=nullptr;
    QDoubleSpinBox* m_range=nullptr;
    QSpinBox* m_azimuth=nullptr;
    QSpinBox* m_elevations=nullptr;
    QDoubleSpinBox* m_minElevation=nullptr;
    QDoubleSpinBox* m_maxElevation=nullptr;
    QDoubleSpinBox* m_spread=nullptr;
    QDoubleSpinBox* m_exposed=nullptr;
    QDoubleSpinBox* m_attenuation=nullptr;
    QDoubleSpinBox* m_density=nullptr;
    QCheckBox* m_empirical=nullptr;
    QCheckBox* m_neighbours=nullptr;
    QCheckBox* m_compass=nullptr;
};
