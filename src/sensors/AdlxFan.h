#pragma once
#include <QString>
#include <QVector>
#include <memory>

namespace NZXTKrakenPump {

struct AdlxChannel {
    QString id;
    QString rpmId;
    QString name;    // "GPU Fan"
    QString hw;
};

class AdlxFan {
public:
    AdlxFan();
    ~AdlxFan();

    bool init();
    void shutdown();
    const QVector<AdlxChannel>& channels() const { return m_channels; }

    static bool owns(const QString& id) { return id.startsWith(QLatin1String("adlx/")); }
    float value(const QString& id) const;
    bool  setDuty(const QString& id, int percent);
    void  setDefault(const QString& id);

private:
    struct Impl;
    std::unique_ptr<Impl> m_d;
    QVector<AdlxChannel>  m_channels;
};

}
