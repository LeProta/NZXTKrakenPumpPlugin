#pragma once
#include <QObject>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QPoint>
#include <QStringList>
#include <QElapsedTimer>
#include <QMetaType>
#include "../sensors/LhwmFans.h"

class QTimer;

namespace NZXTKrakenPump {

class KrakenPumpDevice;

enum FanMode {
    FanSilent = 0,
    FanPerformance,
    FanFixed,
    FanCustom,
};

struct FanChannelConfig {
    int             mode = FanSilent;
    QString         sourceId;
    QVector<QPoint> points;
    int             fixedDuty = 50;
    QString         name;
    int             riseRate = 5;
    int             fallRate = 5;
    int             zeroBelow = 0;
    int             startDuty = 0;
};

inline const QString KRAKEN_FAN_ID  = QStringLiteral("kraken/fan");
inline const QString KRAKEN_PUMP_ID = QStringLiteral("kraken/pump");

class PumpWorker : public QObject {
    Q_OBJECT
public:
    explicit PumpWorker(KrakenPumpDevice* device);

public slots:
    void start();
    void stop();
    void setFanConfig(QString id, NZXTKrakenPump::FanChannelConfig cfg);
    void identify(QString id);
    void calibrate();
    void releaseChannel(QString id);

public:
    void presetPumpTachChannels(const QStringList& ids);

signals:
    void deviceOpened(const QString& name, bool ok, bool hasFan);
    void statusText(const QString& text);
    void readings(float liquidTemp, int pumpRpm, int pumpDuty, int fanRpm, int fanDuty);

    void fanChannelsReady(const QStringList& ids, const QStringList& names,
                          const QStringList& groups);
    void tempSourcesReady(const QStringList& ids, const QStringList& names);
    void fanReadings(const QStringList& ids, const QVector<int>& rpms,
                     const QVector<int>& duties);
    void tempValues(const QStringList& ids, const QVector<float>& values);
    void pumpTachChannelsChanged(const QStringList& ids);
    void fanSensorFault(const QStringList& ids, const QStringList& reasons);
    void calibrationProgress(int percent);
    void calibrationFinished(const QMap<QString, QVector<QPoint>>& tables);
    void calibrationSpin(const QStringList& ids, int dutyPercent);

private slots:
    void tick();

private:
    bool tryOpen();
    void fanTick();
    void emitFanChannels();
    void publishLhwmSources();
    void maybeRetryLhwm();
    void finishCalibration();
    float sourceTemp(const QString& id) const;

    KrakenPumpDevice* m_device = nullptr;
    QTimer*           m_timer  = nullptr;

    int           m_failCount     = 0;
    int           m_reopenDelayMs = 3000;
    QElapsedTimer m_reopenClock;

    int           m_lhwmRetriesLeft = 0;
    int           m_lhwmRetryTick   = 0;

    LhwmFans m_lhwm;
    QMap<QString, FanChannelConfig> m_fanCfg;
    QMap<QString, int>              m_lastDuty;
    QSet<QString>                   m_forceWrite;
    QSet<QString>                   m_managed;
    QMap<QString, float>            m_smoothTemp;
    QMap<QString, int>              m_srcBadStreak;
    QStringList                     m_lastFaultIds;
    QStringList                     m_lastFaultReasons;
    QMap<QString, qint64>           m_identifyUntil;
    QElapsedTimer                   m_upClock;

    QSet<QString>                   m_pumpTachChannels;
    QMap<QString, int>              m_pumpTachStreak;
    QMap<QString, int>              m_pumpTachDiverge;
    int                             m_pumpRpm = 0;

    bool                            m_calibActive = false;
    int                             m_calibStep   = 0;
    int                             m_calibSpinStep = -1;
    int                             m_calibTicks  = 0;
    int                             m_calibStable = 0;
    QMap<QString, int>              m_calibPrevRpm;
    QMap<QString, QVector<QPoint>>  m_calibTable;
    QStringList m_srcIds;
    float       m_liquidTemp   = 0.f;
    int         m_krakenFanRpm = 0;
    int         m_krakenFanDuty = 0;
    bool        m_krakenHasFan = false;
};

}

Q_DECLARE_METATYPE(NZXTKrakenPump::FanChannelConfig)
