#pragma once
#include <QString>
#include <QVector>
#include "AdlxFan.h"
#include "NvmlFan.h"

namespace NZXTKrakenPump {

struct LhwmChannel {
    QString id;
    QString rpmId;
    QString name;
    QString hw;
};

struct LhwmTemp {
    QString id;
    QString name;
};

class LhwmFans {
public:
    bool init();
    bool ready() const { return m_ready; }

    int lhwmChannelCount() const { return m_lhwmChannels; }

    const QVector<LhwmChannel>& channels() const { return m_channels; }
    const QVector<LhwmTemp>&    temps()    const { return m_temps; }

    float value(const QString& id) const;
    bool  setDuty(const QString& id, int percent);
    void  setDefault(const QString& id);

private:
    bool initLhwm();

    QVector<LhwmChannel> m_channels;
    QVector<LhwmTemp>    m_temps;
    bool                 m_ready = false;
    int                  m_lhwmChannels = 0;
    mutable NvmlFan      m_nvml;     // GPU NVIDIA via NVML (ids "nvml/…")
    mutable AdlxFan      m_adlx;     // GPU AMD via ADLX (ids "adlx/…")
};

}
