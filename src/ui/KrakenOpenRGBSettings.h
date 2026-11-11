#pragma once
#include <QString>
#include <QJsonObject>

class OpenRGBPluginAPIInterface;

namespace NZXTKrakenPump {

namespace OpenRGBSettings {

// Call from Plugin::Load(); settings then come from OpenRGB's API, not OpenRGB.json.
void init(OpenRGBPluginAPIInterface* api);

// Re-reads the cached settings; call from SettingsManagerUpdated().
void refresh();

QJsonObject section(const QString& key);

QString configDir();

QString language();

bool prefersFahrenheit();

} // namespace OpenRGBSettings
}
