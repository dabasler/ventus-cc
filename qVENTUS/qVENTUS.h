#pragma once
#include <ccStdPluginInterface.h>

class qVENTUS : public QObject, public ccStdPluginInterface {
    Q_OBJECT
    Q_INTERFACES(ccPluginInterface ccStdPluginInterface)
    Q_PLUGIN_METADATA(IID "cccorp.cloudcompare.plugin.qVENTUS" FILE "info.json")
public:
    explicit qVENTUS(QObject* parent=nullptr);
    void onNewSelection(const ccHObject::Container& selectedEntities) override;
    QList<QAction*> getActions() override;
    void registerCommands(ccCommandLineInterface* commandLine) override;
private:
    void calculateAction();
    void exportAction();
    void cleanupAction();
    QAction* m_calculate=nullptr;
    QAction* m_export=nullptr;
    QAction* m_cleanup=nullptr;
};
