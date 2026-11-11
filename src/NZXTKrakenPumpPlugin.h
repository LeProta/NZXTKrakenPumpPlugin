#pragma once
#include "OpenRGBPluginInterface.h"

namespace NZXTKrakenPump { class PumpWidget; }

class NZXTKrakenPumpPlugin
    : public QObject
    , public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "NZXTKrakenPumpPlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    NZXTKrakenPumpPlugin()  = default;
    ~NZXTKrakenPumpPlugin() override;

    OpenRGBPluginInfo   GetPluginInfo()                                      override;
    unsigned int        GetPluginAPIVersion()                                override;
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr)    override;
    QWidget*            GetWidget()                                          override;
    QMenu*              GetTrayMenu()                                        override;
    void                Unload()                                             override;
    void                OnProfileAboutToLoad()                              override {}
    void                OnProfileLoad(nlohmann::json)                       override {}
    nlohmann::json      OnProfileSave()                                     override { return {}; }
    unsigned char*      OnSDKCommand(unsigned int, unsigned char*, unsigned int*) override { return nullptr; }
    void                ProfileManagerUpdated(unsigned int)                 override {}
    void                ResourceManagerUpdated(unsigned int)                override {}
    void                SettingsManagerUpdated(unsigned int)                override;

private:
    NZXTKrakenPump::PumpWidget* m_widget = nullptr;
};
