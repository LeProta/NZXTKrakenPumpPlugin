#pragma once
#include <QString>
#include <QVector>
#include <memory>

namespace NZXTKrakenPump {

inline int nvmlFloorLevel(unsigned tempC, int prev)
{
    if (tempC >= 85 || (prev == 100 && tempC >= 82)) return 100;
    if (tempC >= 75 || (prev >= 60 && tempC >= 72)) return 60;
    return 0;
}
struct NvmlChannel {
    QString id;
    QString rpmId;
    QString name;    // "GPU Fan"
    QString hw;
};

class NvmlFan {
public:
    NvmlFan();
    ~NvmlFan();

    bool init();
    void shutdown();
    const QVector<NvmlChannel>& channels() const { return m_channels; }

    static bool owns(const QString& id) { return id.startsWith(QLatin1String("nvml/")); }
    float value(const QString& id) const;
    bool  setDuty(const QString& id, int percent);
    void  setDefault(const QString& id);

private:
    struct Impl;
    std::unique_ptr<Impl> m_d;
    QVector<NvmlChannel>  m_channels;
};

}
