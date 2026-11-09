#include "PumpWorker.h"
#include "../device/KrakenPumpDevice.h"
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace NZXTKrakenPump {

static int interpDuty(const QVector<QPoint>& pts, float t)
{
    if (pts.isEmpty()) return 50;
    if (t <= pts.first().x()) return pts.first().y();
    if (t >= pts.last().x())  return pts.last().y();
    for (int k = 1; k < pts.size(); ++k) {
        if (t <= pts[k].x()) {
            const QPoint a = pts[k - 1], b = pts[k];
            if (a.x() == b.x())
                return b.y();
            return int(std::lround(a.y() + double(b.y() - a.y())
                       * (t - a.x()) / double(b.x() - a.x())));
        }
    }
    return pts.last().y();
}

static inline bool tempValid(float t) { return t > 0.f && t < 150.f; }

static constexpr int kFaultTicks = 8;

static constexpr int kLiquidSafetyC = 50;
static std::array<uint8_t, 60> krakenTable(int duty)
{
    std::array<uint8_t, 60> t{};
    for (int c = 0; c < 60; ++c)
        t[c] = static_cast<uint8_t>(c >= kLiquidSafetyC ? 100 : duty);
    return t;
}

static constexpr int CALIB_DUTIES[]  = { 100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0 };
static constexpr int CALIB_STEPS     = int(sizeof(CALIB_DUTIES) / sizeof(int));
static constexpr int CALIB_MIN_TICKS = 4;
static constexpr int CALIB_MAX_TICKS = 30;

PumpWorker::PumpWorker(KrakenPumpDevice* device)
    : m_device(device)
{
}

void PumpWorker::start()
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &PumpWorker::tick);

    const bool krakenOk = tryOpen();
    if (!krakenOk) {
        emit deviceOpened(QString(), false, false);
        emit statusText(QStringLiteral(
            "Check the USB connection and close NZXT CAM — motherboard fans remain available."));
        m_reopenClock.start();
    }

    m_lhwm.init();
    publishLhwmSources();

    if (m_lhwm.lhwmChannelCount() == 0) {
        m_lhwmRetriesLeft = 8;
        m_lhwmRetryTick   = 8;
        qWarning() << "[KrakenPump/Fans] no fan channels at startup — "
                      "will retry LHM init (driver may not be ready yet).";
    }

    m_timer->start();
}

void PumpWorker::publishLhwmSources()
{
    m_srcIds.clear();
    QStringList srcNames;
    m_srcIds << QStringLiteral("liquid");
    srcNames << QStringLiteral("Liquid");
    for (const auto& t : m_lhwm.temps()) {
        m_srcIds << t.id;
        srcNames << t.name;
    }
    emit tempSourcesReady(m_srcIds, srcNames);
    emitFanChannels();
}

void PumpWorker::stop()
{
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }

    for (const QString& id : m_managed) {
        m_lhwm.setDefault(id);
        qInfo() << "[KrakenPump/Fans] control returned to BIOS:" << id;
    }
    m_managed.clear();

    if (m_device) m_device->close();
}

void PumpWorker::emitFanChannels()
{
    QStringList ids, names, groups;
    const QString krakenGroup = m_device->isOpen() ? m_device->displayName()
                                                   : QStringLiteral("NZXT Kraken");
    if (m_krakenHasFan) {
        ids << KRAKEN_FAN_ID;
        names << QStringLiteral("Fan");
        groups << krakenGroup;
    }
    for (const auto& ch : m_lhwm.channels()) {
        if (m_pumpTachChannels.contains(ch.id))
            continue;
        ids << ch.id;
        names << ch.name;
        groups << ch.hw;
    }
    emit fanChannelsReady(ids, names, groups);
}

bool PumpWorker::tryOpen()
{
    if (!m_device->open())
        return false;

    m_device->initialize();

    const bool hasFan = m_device->info() && m_device->info()->hasFanChannel;
    m_failCount     = 0;
    m_reopenDelayMs = 3000;
    m_krakenHasFan  = hasFan;
    m_lastDuty.remove(KRAKEN_FAN_ID);
    m_lastDuty.remove(KRAKEN_PUMP_ID);
    emit deviceOpened(m_device->displayName(), true, hasFan);
    emitFanChannels();
    return true;
}

void PumpWorker::setFanConfig(QString id, FanChannelConfig cfg)
{
    m_fanCfg[id] = cfg;
    m_lastDuty.remove(id);
    m_smoothTemp.remove(id);
}

void PumpWorker::presetPumpTachChannels(const QStringList& ids)
{
    for (const QString& id : ids)
        m_pumpTachChannels.insert(id);
}

void PumpWorker::releaseChannel(QString id)
{
    m_fanCfg.remove(id);
    if (m_managed.remove(id))
        m_lhwm.setDefault(id);
    m_lastDuty.remove(id);
    m_smoothTemp.remove(id);
    m_forceWrite.remove(id);
    m_srcBadStreak.remove(id);
    m_identifyUntil.remove(id);
}

void PumpWorker::calibrate()
{
    if (m_calibActive)
        return;
    m_calibTable.clear();
    m_calibPrevRpm.clear();
    m_calibActive = true;
    m_calibStep   = 0;
    m_calibSpinStep = -1;
    m_calibTicks  = 0;
    m_calibStable = 0;
    qInfo() << "[KrakenPump/Fans] calibration started (100%..0% by 10,"
               " sampling once RPM settle)";
}

void PumpWorker::identify(QString id)
{
    if (m_calibActive)
        return;
    if (!m_upClock.isValid()) m_upClock.start();
    m_identifyUntil[id] = m_upClock.elapsed() + 5000;
    m_lastDuty.remove(id);
    qInfo() << "[KrakenPump/Fans] identify:" << id;
}

float PumpWorker::sourceTemp(const QString& id) const
{
    if (id == QLatin1String("liquid"))
        return m_liquidTemp;
    return m_lhwm.value(id);
}

void PumpWorker::fanTick()
{
    QVector<float> vals;
    QHash<QString, float> tickTemp;
    vals.reserve(m_srcIds.size());
    tickTemp.reserve(m_srcIds.size());
    for (const QString& id : m_srcIds) {
        const float v = sourceTemp(id);
        vals.append(v);
        tickTemp.insert(id, v);
    }
    emit tempValues(m_srcIds, vals);

    QStringList ids;
    QVector<int> rpms, duties;
    QHash<QString, int> tickRpm;
    bool channelsChanged = false;
    if (m_krakenHasFan) {
        ids << KRAKEN_FAN_ID;
        rpms << m_krakenFanRpm;
        duties << m_krakenFanDuty;
        tickRpm.insert(KRAKEN_FAN_ID, m_krakenFanRpm);
    }
    for (const auto& ch : m_lhwm.channels()) {
        const int rpm  = int(std::lround(m_lhwm.value(ch.rpmId)));
        const int duty = int(std::lround(m_lhwm.value(ch.id)));
        tickRpm.insert(ch.id, rpm);

        if (!ch.rpmId.isEmpty() && !m_calibActive && m_pumpRpm > 500) {
            const int  tol   = std::max(40, m_pumpRpm / 50);
            const bool match = (rpm > 0 && std::abs(rpm - m_pumpRpm) <= tol);
            if (!m_pumpTachChannels.contains(ch.id)) {
                if (match) {
                    const int streak = m_pumpTachStreak.value(ch.id, 0) + 1;
                    m_pumpTachStreak[ch.id] = streak;
                    if (streak >= 15) {
                        m_pumpTachStreak.remove(ch.id);
                        m_pumpTachChannels.insert(ch.id);
                        m_lhwm.setDefault(ch.id);
                        m_managed.remove(ch.id);
                        qInfo() << "[KrakenPump/Fans] hiding channel — tachometer"
                                   " follows the Kraken pump RPM:" << ch.id;
                        channelsChanged = true;
                        emit pumpTachChannelsChanged(
                            QStringList(m_pumpTachChannels.values()));
                    }
                } else {
                    m_pumpTachStreak.remove(ch.id);
                }
            } else if (!match) {
                const int d = m_pumpTachDiverge.value(ch.id, 0) + 1;
                m_pumpTachDiverge[ch.id] = d;
                if (d >= 10) {
                    m_pumpTachDiverge.remove(ch.id);
                    m_pumpTachChannels.remove(ch.id);
                    m_lastDuty.remove(ch.id);
                    qInfo() << "[KrakenPump/Fans] channel no longer follows the"
                               " pump RPM — back alive:" << ch.id;
                    channelsChanged = true;
                    emit pumpTachChannelsChanged(
                        QStringList(m_pumpTachChannels.values()));
                }
            } else {
                m_pumpTachDiverge.remove(ch.id);
            }
        }

        if (m_pumpTachChannels.contains(ch.id))
            continue;
        ids << ch.id;
        rpms << rpm;
        duties << duty;
    }
    emit fanReadings(ids, rpms, duties);
    if (channelsChanged)
        emitFanChannels();

    auto writeDuty = [this](const QString& id, int duty) -> bool {
        if (id == KRAKEN_FAN_ID) {
            if (!m_krakenHasFan || !m_device->isOpen()) return false;
            if (!m_device->setCurve(Channel::Fan, krakenTable(duty))) return false;
        } else if (id == KRAKEN_PUMP_ID) {
            if (!m_device->isOpen()) return false;
            if (!m_device->setCurve(Channel::Pump, krakenTable(duty))) return false;
        } else {
            m_managed.insert(id);
            if (!m_lhwm.setDuty(id, duty)) {
                static int s_err = 0;
                if (s_err++ < 5)
                    qWarning() << "[KrakenPump/Fans] setDuty failed:" << id << duty;
                return false;
            }
        }
        m_lastDuty[id] = duty;
        m_forceWrite.remove(id);
        return true;
    };

    if (!m_upClock.isValid()) m_upClock.start();
    const qint64 nowMs = m_upClock.elapsed();

    if (m_calibActive) {
        QStringList calibIds;
        if (m_krakenHasFan && m_fanCfg.contains(KRAKEN_FAN_ID))
            calibIds << KRAKEN_FAN_ID;
        for (const auto& ch : m_lhwm.channels())
            if (!ch.rpmId.isEmpty() && !m_pumpTachChannels.contains(ch.id)
                    && m_fanCfg.contains(ch.id))
                calibIds << ch.id;

        const int duty = CALIB_DUTIES[m_calibStep];
        for (const QString& id : calibIds)
            if (m_lastDuty.value(id, -1) != duty)
                writeDuty(id, duty);

        if (m_calibSpinStep != m_calibStep) {
            m_calibSpinStep = m_calibStep;
            emit calibrationSpin(calibIds, duty);
        }

        QMap<QString, int> cur;
        for (const QString& id : calibIds)
            cur.insert(id, tickRpm.value(id, 0));
        ++m_calibTicks;
        bool stable = (m_calibTicks >= CALIB_MIN_TICKS);
        if (stable) {
            for (auto it = cur.constBegin(); it != cur.constEnd(); ++it) {
                const int prev = m_calibPrevRpm.value(it.key(), -1);
                if (prev < 0
                        || std::abs(it.value() - prev) > std::max(20, prev / 33)) {
                    stable = false;
                    break;
                }
            }
        }
        m_calibStable  = stable ? m_calibStable + 1 : 0;
        m_calibPrevRpm = cur;

        if (m_calibStable >= 3 || m_calibTicks >= CALIB_MAX_TICKS) {
            for (auto it = cur.constBegin(); it != cur.constEnd(); ++it)
                m_calibTable[it.key()].append(QPoint(duty, it.value()));
            m_calibStep++;
            m_calibTicks  = 0;
            m_calibStable = 0;
            m_calibPrevRpm.clear();
            if (m_calibStep >= CALIB_STEPS)
                finishCalibration();
        }
        if (m_calibActive) {
            const qreal frac = std::min(1.0, m_calibTicks / qreal(CALIB_MIN_TICKS + 3));
            emit calibrationProgress(
                std::min(99, int(100.0 * (m_calibStep + frac) / CALIB_STEPS)));
        }
    }

    for (auto it = m_identifyUntil.begin(); it != m_identifyUntil.end(); ) {
        const QString id = it.key();
        if (nowMs < it.value()) {
            if (!m_calibActive && m_lastDuty.value(id, -1) != 100)
                writeDuty(id, 100);
            ++it;
            continue;
        }
        it = m_identifyUntil.erase(it);
        m_lastDuty.remove(id);
    }

    QStringList faultIds, faultReasons;
    const auto faultReason = [this](const QString& sid) -> QString {
        if (sid.isEmpty())
            return QStringLiteral("No temperature source selected.");
        if (sid == QLatin1String("liquid"))
            return QStringLiteral("Kraken not connected — liquid temperature unavailable.");
        if (sid.startsWith(QLatin1String("/gpu"))
                || sid.contains(QLatin1String("GPU")))
            return QStringLiteral("GPU sensors unavailable — GPU driver not "
                                  "installed or not loaded.");
        if (!m_srcIds.contains(sid))
            return QStringLiteral("Temperature source no longer available.");
        return QStringLiteral("Temperature sensor not responding.");
    };

    for (auto it = m_fanCfg.constBegin(); it != m_fanCfg.constEnd(); ++it) {
        const QString& id = it.key();
        const FanChannelConfig& cfg = it.value();
        if (m_identifyUntil.contains(id)) {
            const int li = m_lastFaultIds.indexOf(id);
            if (li >= 0) { faultIds << id; faultReasons << m_lastFaultReasons.value(li); }
            continue;
        }
        if (m_pumpTachChannels.contains(id))
            continue;
        if (m_calibActive && id != KRAKEN_PUMP_ID)
            continue;

        const auto cached = tickTemp.constFind(cfg.sourceId);
        const float raw = (cached != tickTemp.constEnd()) ? cached.value()
                                                          : sourceTemp(cfg.sourceId);
        if (!tempValid(raw)) {
            m_smoothTemp.remove(id);
            const int n = m_srcBadStreak.value(id, 0) + 1;
            m_srcBadStreak.insert(id, n);
            if (n >= kFaultTicks) {
                faultIds << id;
                faultReasons << faultReason(cfg.sourceId);
                if (m_managed.remove(id)) {
                    m_lhwm.setDefault(id);
                    m_lastDuty.remove(id);
                    m_forceWrite.remove(id);
                }
            }
            continue;
        }
        m_srcBadStreak.remove(id);
        float t;
        auto st = m_smoothTemp.find(id);
        if (st == m_smoothTemp.end()) {
            m_smoothTemp.insert(id, raw);
            t = raw;
        } else {
            st.value() += 0.4f * (raw - st.value());
            t = st.value();
        }

        int target = std::clamp(interpDuty(cfg.points, t), 0, 100);

        const bool isPump = (id == KRAKEN_PUMP_ID);
        const int  last   = m_lastDuty.value(id, -1);

        const int dutyFloor = (cfg.zeroBelow > 0) ? cfg.zeroBelow
                            : (cfg.startDuty > 0) ? cfg.startDuty : 0;
        if (isPump) {
            target = std::max(target, 20);
        } else if (target > 0 && target < dutyFloor) {
            target = 0;
        }

        if (last >= 0) {
            if (target > last)
                target = std::min(target, last + std::max(1, cfg.riseRate));
            else if (target < last)
                target = std::max(target, last - std::max(1, cfg.fallRate));
        }

        const bool force = m_forceWrite.contains(id);
        if (!force) {
            if (last == target)
                continue;
            if (last >= 0 && std::abs(target - last) <= 1
                    && target != 0 && target != 100)
                continue;
        }

        writeDuty(id, target);
    }

    if (faultIds != m_lastFaultIds) {
        m_lastFaultIds = faultIds;
        m_lastFaultReasons = faultReasons;
        emit fanSensorFault(faultIds, faultReasons);
    }
}

void PumpWorker::finishCalibration()
{
    m_calibActive = false;
    QMap<QString, QVector<QPoint>> tables;
    bool tachChanged = false;
    for (auto it = m_calibTable.constBegin(); it != m_calibTable.constEnd(); ++it) {
        const QString&          id  = it.key();
        const QVector<QPoint>&  tbl = it.value();
        int rpmHigh = 0, rpmLow = 1 << 30;
        for (const QPoint& p : tbl) {
            rpmHigh = std::max(rpmHigh, p.y());
            rpmLow  = std::min(rpmLow,  p.y());
        }
        if (id != KRAKEN_FAN_ID && rpmHigh > 0
                && (rpmHigh - rpmLow) < std::max(60, rpmHigh / 10)) {
            m_pumpTachChannels.insert(id);
            m_lhwm.setDefault(id);
            m_managed.remove(id);
            qInfo() << "[KrakenPump/Fans] calibration: RPM does not respond to"
                       " duty — channel hidden (external tach):" << id
                    << "range" << rpmLow << ".." << rpmHigh;
            tachChanged = true;
            continue;
        }
        if (rpmHigh > 0) {
            tables.insert(id, tbl);
            qInfo() << "[KrakenPump/Fans] calibration:" << id
                    << "RPM" << rpmLow << ".." << rpmHigh << "over" << tbl.size()
                    << "steps";
        }
    }
    if (tachChanged) {
        emit pumpTachChannelsChanged(QStringList(m_pumpTachChannels.values()));
        emitFanChannels();
    }
    emit calibrationSpin(QStringList(), 0);
    emit calibrationProgress(100);
    emit calibrationFinished(tables);
    qInfo() << "[KrakenPump/Fans] calibration finished —" << tables.size()
            << "fan(s) measured";
}

void PumpWorker::maybeRetryLhwm()
{
    if (m_lhwmRetriesLeft <= 0 || m_calibActive)
        return;
    if (--m_lhwmRetryTick > 0)
        return;
    m_lhwmRetryTick = 8;
    --m_lhwmRetriesLeft;
    m_lhwm.init();
    if (m_lhwm.lhwmChannelCount() > 0) {
        m_lhwmRetriesLeft = 0;
        qInfo() << "[KrakenPump/Fans] LHM ready on retry:"
                << m_lhwm.channels().size() << "fan channel(s).";
        publishLhwmSources();
    } else if (m_lhwmRetriesLeft == 0) {
        qWarning() << "[KrakenPump/Fans] no fan channels after retries "
                      "— motherboard fans unavailable this session.";
    }
}

void PumpWorker::tick()
{
    if (m_device->isOpen()) {
        PumpReadings r;
        if (m_device->readStatus(r)) {
            m_failCount     = 0;
            m_liquidTemp    = r.liquidTemp;
            m_pumpRpm       = r.pumpRPM;
            m_krakenFanRpm  = r.fanRPM;
            m_krakenFanDuty = r.fanDuty;
            emit readings(r.liquidTemp, r.pumpRPM, r.pumpDuty, r.fanRPM, r.fanDuty);

            const bool hot = r.liquidTemp >= kLiquidSafetyC;
            const auto expected = [&](const QString& id) {
                return hot ? 100 : m_lastDuty.value(id);
            };
            if (m_lastDuty.contains(KRAKEN_PUMP_ID)
                    && r.pumpDuty != expected(KRAKEN_PUMP_ID))
                m_forceWrite.insert(KRAKEN_PUMP_ID);
            if (m_lastDuty.contains(KRAKEN_FAN_ID)
                    && r.fanDuty != expected(KRAKEN_FAN_ID))
                m_forceWrite.insert(KRAKEN_FAN_ID);
        } else if (++m_failCount >= 3) {
            qWarning() << "[KrakenPump/Worker] status read failed 3× — device lost.";
            m_device->close();
            m_failCount     = 0;
            m_reopenDelayMs = 3000;
            m_reopenClock.start();
            m_krakenHasFan  = false;
            m_liquidTemp    = 0.f;
            m_pumpRpm       = 0;
            if (m_calibActive) {
                m_calibActive = false;
                m_calibTable.clear();
                m_calibPrevRpm.clear();
                qWarning() << "[KrakenPump/Fans] calibration aborted — device lost.";
                emit calibrationSpin(QStringList(), 0);
                emit calibrationFinished(QMap<QString, QVector<QPoint>>());
            }
            emit deviceOpened(QString(), false, false);
            emit statusText(QStringLiteral("Device lost — reconnecting…"));
            emitFanChannels();
        }
        fanTick();
        maybeRetryLhwm();
        return;
    }

    fanTick();
    maybeRetryLhwm();

    if (m_reopenClock.isValid() && m_reopenClock.elapsed() < m_reopenDelayMs)
        return;
    m_reopenClock.start();

    if (!KrakenPumpDevice::anySupportedPresent()) {
        m_reopenDelayMs = 3000;
        return;
    }
    if (!tryOpen())
        m_reopenDelayMs = std::min(m_reopenDelayMs * 2, 30000);
}

}
