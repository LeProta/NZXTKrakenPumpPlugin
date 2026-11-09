#include "AdlxFan.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <algorithm>
#include "GpuFanState.h"

#include "SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "SDK/Include/IGPUManualFanTuning.h"
#include "SDK/Include/IGPUTuning.h"
#include "SDK/Include/IPerformanceMonitoring.h"

using namespace adlx;

namespace NZXTKrakenPump {

namespace {
struct Gpu {
    IADLXGPUPtr             gpu;
    IADLXManualFanTuningPtr tuning;
    QString                 name;
    ADLX_IntRange           speedRange{};
    ADLX_IntRange           tempRange{};
    QVector<QPoint>         orig;
    adlx_bool               zeroSupported = false;
    adlx_bool               zeroOrig = false;
    adlx_bool               zeroNow  = false;
    int                     lastDuty = -1;
    bool                    touched  = false;
    bool                    failLogged = false;
};
} // namespace

struct AdlxFan::Impl {
    ADLXHelper                         helper;
    bool                               helperOk = false;
    IADLXGPUTuningServicesPtr          tuningSvc;
    IADLXPerformanceMonitoringServicesPtr perf;
    QVector<Gpu>                       gpus;

    Gpu* find(const QString& id, bool* isFan = nullptr)
    {
        const QStringList p = id.split(QLatin1Char('/'));
        if (p.size() != 3 || p[0] != QLatin1String("adlx")) return nullptr;
        bool ok = false;
        const int n = p[1].toInt(&ok);
        if (!ok || n < 0 || n >= gpus.size()) return nullptr;
        if (isFan) *isFan = (p[2] == QLatin1String("fan"));
        return &gpus[n];
    }

    bool writeStates(Gpu& g, const QVector<QPoint>& pts)
    {
        IADLXManualFanTuningStateListPtr states;
        if (ADLX_FAILED(g.tuning->GetFanTuningStates(&states)) || !states)
            return false;
        const adlx_uint n = states->Size();
        if (n == 0 || n != adlx_uint(pts.size()))
            return false;
        for (adlx_uint i = 0; i < n; ++i) {
            IADLXManualFanTuningStatePtr st;
            if (ADLX_FAILED(states->At(i, &st)) || !st) return false;
            const ADLX_RESULT r = st->SetFanSpeed(pts[int(i)].y());
            if (r == ADLX_RESET_NEEDED) {
                if (!g.failLogged)
                    qWarning() << "[KrakenPump/Fans] ADLX asks for a factory reset to"
                                  " change the fan table — refused (it would erase"
                                  " Adrenalin tuning). GPU fan left untouched.";
                g.failLogged = true;
                return false;
            }
            if (ADLX_FAILED(r)
                || (pts[int(i)].x() >= 0 && ADLX_FAILED(st->SetTemperature(pts[int(i)].x()))))
                return false;
        }
        adlx_int errIdx = -1;
        if (ADLX_FAILED(g.tuning->IsValidFanTuningStates(states, &errIdx))) {
            if (!g.failLogged)
                qWarning() << "[KrakenPump/Fans] ADLX rejected the fan table at state"
                           << errIdx;
            g.failLogged = true;
            return false;
        }
        if (ADLX_FAILED(g.tuning->SetFanTuningStates(states))) {
            if (!g.failLogged)
                qWarning() << "[KrakenPump/Fans] ADLX SetFanTuningStates failed.";
            g.failLogged = true;
            return false;
        }
        g.failLogged = false;
        return true;
    }
};

AdlxFan::AdlxFan() : m_d(new Impl) {}
AdlxFan::~AdlxFan() { shutdown(); }

bool AdlxFan::init()
{
    if (m_d->helperOk) {
        return !m_channels.isEmpty();
    }
    if (ADLX_FAILED(m_d->helper.Initialize()))
        return false;
    m_d->helperOk = true;

    IADLXSystem* sys = m_d->helper.GetSystemServices();
    IADLXGPUListPtr list;
    if (!sys
        || ADLX_FAILED(sys->GetGPUTuningServices(&m_d->tuningSvc)) || !m_d->tuningSvc
        || ADLX_FAILED(sys->GetPerformanceMonitoringServices(&m_d->perf)) || !m_d->perf
        || ADLX_FAILED(sys->GetGPUs(&list)) || !list) {
        shutdown();
        return false;
    }

    for (adlx_uint i = list->Begin(); i != list->End(); ++i) {
        Gpu g;
        if (ADLX_FAILED(list->At(i, &g.gpu)) || !g.gpu) continue;
        const char* nm = nullptr;
        g.gpu->Name(&nm);
        g.name = nm ? QString::fromUtf8(nm) : QStringLiteral("AMD GPU");

        adlx_bool supported = false;
        if (ADLX_FAILED(m_d->tuningSvc->IsSupportedManualFanTuning(g.gpu, &supported))
            || !supported) {
            qInfo() << "[KrakenPump/Fans] ADLX:" << g.name
                    << "— manual fan tuning not supported, skipped.";
            continue;
        }
        IADLXInterfacePtr ifc;
        if (ADLX_FAILED(m_d->tuningSvc->GetManualFanTuning(g.gpu, &ifc)) || !ifc)
            continue;
        g.tuning = IADLXManualFanTuningPtr(ifc);
        if (!g.tuning
            || ADLX_FAILED(g.tuning->GetFanTuningRanges(&g.speedRange, &g.tempRange)))
            continue;

        IADLXManualFanTuningStateListPtr cur;
        if (ADLX_FAILED(g.tuning->GetFanTuningStates(&cur)) || !cur) continue;
        for (adlx_uint k = cur->Begin(); k != cur->End(); ++k) {
            IADLXManualFanTuningStatePtr st;
            adlx_int sp = 0, tp = 0;
            if (ADLX_SUCCEEDED(cur->At(k, &st)) && st) {
                st->GetFanSpeed(&sp);
                st->GetTemperature(&tp);
            }
            g.orig.append(QPoint(tp, sp));
        }
        if (g.orig.isEmpty()) continue;

        g.tuning->IsSupportedZeroRPM(&g.zeroSupported);
        if (g.zeroSupported) {
            g.tuning->GetZeroRPMState(&g.zeroOrig);
            g.zeroNow = g.zeroOrig;
        }

        const QString rf = gpuFanStatePath(QStringLiteral("adlx_recovery"), g.name);
        if (QFile::exists(rf)) {
            QVector<QPoint> saved; bool savedZero = false;
            QFile f(rf);
            const bool parsed = f.open(QIODevice::ReadOnly)
                                && adlxParse(f.readAll(), saved, savedZero);
            f.close();
            if (parsed && g.orig == saved) {
                QFile::remove(rf);
                g.zeroOrig = savedZero;
            } else if (parsed && adlxLooksLikePluginTable(g.orig, saved)) {
                bool ok = m_d->writeStates(g, saved);
                if (ok && g.zeroSupported) {
                    ok = ADLX_SUCCEEDED(g.tuning->SetZeroRPMState(savedZero));
                    if (ok) g.zeroNow = savedZero;
                }
                qInfo() << "[KrakenPump/Fans] ADLX:" << g.name
                        << (ok ? "— stock fan table restored after an unclean exit."
                               : "— could not restore stock fan table after an unclean exit.");
                g.orig = saved;
                g.zeroOrig = savedZero;
                if (ok) QFile::remove(rf); else g.touched = true;
            } else {
                if (parsed)
                    qInfo() << "[KrakenPump/Fans] ADLX:" << g.name
                            << "— recovery file ignored (fan table was changed since).";
                QFile::remove(rf);
            }
        }

        qInfo().noquote() << QStringLiteral(
            "[KrakenPump/Fans] ADLX: %1 — fan speed %2..%3 %, temperature %4..%5 °C, "
            "%6 states, zero-RPM %7")
            .arg(g.name).arg(g.speedRange.minValue).arg(g.speedRange.maxValue)
            .arg(g.tempRange.minValue).arg(g.tempRange.maxValue).arg(g.orig.size())
            .arg(g.zeroSupported ? QStringLiteral("supported") : QStringLiteral("n/a"));
        m_d->gpus.append(std::move(g));
    }

    if (m_d->gpus.isEmpty()) {
        shutdown();
        return false;
    }
    m_channels.clear();
    for (int i = 0; i < m_d->gpus.size(); ++i) {
        AdlxChannel ch;
        ch.id    = QStringLiteral("adlx/%1/control").arg(i);
        ch.rpmId = QStringLiteral("adlx/%1/fan").arg(i);
        ch.name  = QStringLiteral("GPU Fan");
        ch.hw    = m_d->gpus[i].name;
        m_channels.append(ch);
    }
    return true;
}

void AdlxFan::shutdown()
{
    for (Gpu& g : m_d->gpus)
        if (g.touched && g.tuning)
            setDefault(QStringLiteral("adlx/%1/control").arg(&g - m_d->gpus.data()));
    m_d->gpus.clear();
    m_d->perf       = nullptr;
    m_d->tuningSvc  = nullptr;
    m_channels.clear();
    if (m_d->helperOk) {
        m_d->helper.Terminate();
        m_d->helperOk = false;
    }
}

float AdlxFan::value(const QString& id) const
{
    bool isFan = false;
    Gpu* g = m_d->find(id, &isFan);
    if (!g) return 0.f;
    if (!isFan)
        return float(std::max(g->lastDuty, 0));
    IADLXGPUMetricsPtr m;
    adlx_int rpm = 0;
    if (m_d->perf && ADLX_SUCCEEDED(m_d->perf->GetCurrentGPUMetrics(g->gpu, &m)) && m)
        m->GPUFanSpeed(&rpm);
    return float(rpm);
}

bool AdlxFan::setDuty(const QString& id, int percent)
{
    Gpu* g = m_d->find(id);
    if (!g) return false;

    const int lo = g->speedRange.minValue, hi = g->speedRange.maxValue;
    const bool below = percent < lo;
    const int  speed = std::clamp(percent, lo, hi);

    if (!g->touched) {
        const QString rf = gpuFanStatePath(QStringLiteral("adlx_recovery"), g->name);
        if (!gpuFanStateWrite(rf, adlxSerialize(g->orig, g->zeroOrig)))
            qWarning() << "[KrakenPump/Fans] ADLX: cannot write recovery file" << rf;
        g->touched = true;
    }

    if (g->zeroSupported) {
        const adlx_bool want = below;
        if (want != g->zeroNow && ADLX_SUCCEEDED(g->tuning->SetZeroRPMState(want)))
            g->zeroNow = want;
    }

    if (!m_d->writeStates(*g, adlxTable(g->orig, speed)))
        return false;
    g->lastDuty = percent;
    return true;
}

void AdlxFan::setDefault(const QString& id)
{
    Gpu* g = m_d->find(id);
    if (!g || !g->touched) return;
    if (g->zeroSupported && g->zeroNow != g->zeroOrig
        && ADLX_SUCCEEDED(g->tuning->SetZeroRPMState(g->zeroOrig)))
        g->zeroNow = g->zeroOrig;
    if (m_d->writeStates(*g, g->orig)) {
        g->touched  = false;
        g->lastDuty = -1;
        QFile::remove(gpuFanStatePath(QStringLiteral("adlx_recovery"), g->name));
    }
}

}
