#include "NvmlFan.h"
#include <QDebug>
#include <QFile>
#include <algorithm>
#include "GpuFanState.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace NZXTKrakenPump {

namespace {
using nvmlReturn_t = int;
using nvmlDevice_t = void*;
constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr unsigned     NVML_TEMPERATURE_GPU = 0;

struct nvmlFanSpeedInfo_t {     // nvmlFanSpeedInfo_v1_t
    unsigned int version;       // sizeof(struct) | (1 << 24)
    unsigned int fan;
    unsigned int speed;         // OUT : RPM
};

using PInit      = nvmlReturn_t (*)();
using PShutdown  = nvmlReturn_t (*)();
using PCount     = nvmlReturn_t (*)(unsigned*);
using PHandle    = nvmlReturn_t (*)(unsigned, nvmlDevice_t*);
using PName      = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned);
using PNumFans   = nvmlReturn_t (*)(nvmlDevice_t, unsigned*);
using PFanPct    = nvmlReturn_t (*)(nvmlDevice_t, unsigned, unsigned*);
using PFanRpm    = nvmlReturn_t (*)(nvmlDevice_t, nvmlFanSpeedInfo_t*);
using PSetFan    = nvmlReturn_t (*)(nvmlDevice_t, unsigned, unsigned);
using PSetDef    = nvmlReturn_t (*)(nvmlDevice_t, unsigned);
using PMinMax    = nvmlReturn_t (*)(nvmlDevice_t, unsigned*, unsigned*);
using PTemp      = nvmlReturn_t (*)(nvmlDevice_t, unsigned, unsigned*);

struct Gpu {
    nvmlDevice_t dev = nullptr;
    QString      name;
    unsigned     fans = 0;
    unsigned     minPct = 0, maxPct = 100;
    int          desired = -1;
    int          applied = -1;
    int          floorLevel = 0;
    int          lastDuty = -1;
    bool         touched = false;
    bool         failLogged = false;
    bool         rpmLogged = false;
    bool         tempLogged = false;
};
} // namespace

struct NvmlFan::Impl {
#ifdef _WIN32
    HMODULE dll = nullptr;
#endif
    bool inited = false;
    PInit fInit = nullptr; PShutdown fShutdown = nullptr; PCount fCount = nullptr;
    PHandle fHandle = nullptr; PName fName = nullptr; PNumFans fNumFans = nullptr;
    PFanPct fFanPct = nullptr; PFanRpm fFanRpm = nullptr; PSetFan fSetFan = nullptr;
    PSetDef fSetDef = nullptr; PMinMax fMinMax = nullptr; PTemp fTemp = nullptr;
    QVector<Gpu> gpus;

    static QString markerPath() { return gpuFanStatePath(QStringLiteral("nvml_recovery")); }

    Gpu* find(const QString& id, bool* isFan = nullptr)
    {
        const QStringList p = id.split(QLatin1Char('/'));
        if (p.size() != 3 || p[0] != QLatin1String("nvml")) return nullptr;
        bool ok = false;
        const int n = p[1].toInt(&ok);
        if (!ok || n < 0 || n >= gpus.size()) return nullptr;
        if (isFan) *isFan = (p[2] == QLatin1String("fan"));
        return &gpus[n];
    }

    bool apply(Gpu& g)
    {
        if (g.desired < 0) return true;
        int level = g.floorLevel;
        unsigned t = 0;
        if (fTemp(g.dev, NVML_TEMPERATURE_GPU, &t) == NVML_SUCCESS) {
            level = nvmlFloorLevel(t, g.floorLevel);
        } else {
            level = std::max(level, 60);
            if (!g.tempLogged)
                qWarning() << "[KrakenPump/Fans] NVML: cannot read GPU temperature for"
                           << g.name << "— fan kept at 60 % minimum.";
            g.tempLogged = true;
        }
        g.floorLevel = level;
        const int want = std::clamp(std::max(g.desired, level), int(g.minPct), int(g.maxPct));
        if (want == g.applied) return true;

        for (unsigned f = 0; f < g.fans; ++f) {
            const nvmlReturn_t r = fSetFan(g.dev, f, unsigned(want));
            if (r != NVML_SUCCESS) {
                if (!g.failLogged)
                    qWarning() << "[KrakenPump/Fans] NVML SetFanSpeed failed, code" << r
                               << "(4 = administrator rights required, 3 = not supported).";
                g.failLogged = true;
                return false;
            }
        }
        g.failLogged = false;
        g.applied = want;
        return true;
    }

    bool restoreAuto(Gpu& g)
    {
        bool ok = true;
        for (unsigned f = 0; f < g.fans; ++f)
            ok = (fSetDef(g.dev, f) == NVML_SUCCESS) && ok;
        return ok;
    }

#ifdef _WIN32
    template <class F> bool sym(F& f, const char* name)
    {
        f = reinterpret_cast<F>(GetProcAddress(dll, name));
        return f != nullptr;
    }
#endif
};

NvmlFan::NvmlFan() : m_d(new Impl) {}
NvmlFan::~NvmlFan() { shutdown(); }

bool NvmlFan::init()
{
#ifdef _WIN32
    if (m_d->inited) return !m_channels.isEmpty();

#ifdef NVML_TEST_DLL_PATH
    m_d->dll = LoadLibraryA(NVML_TEST_DLL_PATH);
#else
    m_d->dll = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
    if (!m_d->dll)
        m_d->dll = LoadLibraryExW(
            L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll", nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m_d->dll) return false;

    Impl& d = *m_d;
    const bool ok = d.sym(d.fInit, "nvmlInit_v2") && d.sym(d.fShutdown, "nvmlShutdown")
        && d.sym(d.fCount, "nvmlDeviceGetCount_v2")
        && d.sym(d.fHandle, "nvmlDeviceGetHandleByIndex_v2")
        && d.sym(d.fName, "nvmlDeviceGetName") && d.sym(d.fNumFans, "nvmlDeviceGetNumFans")
        && d.sym(d.fFanPct, "nvmlDeviceGetFanSpeed_v2")
        && d.sym(d.fSetFan, "nvmlDeviceSetFanSpeed_v2")
        && d.sym(d.fSetDef, "nvmlDeviceSetDefaultFanSpeed_v2")
        && d.sym(d.fMinMax, "nvmlDeviceGetMinMaxFanSpeed")
        && d.sym(d.fTemp, "nvmlDeviceGetTemperature");
    d.sym(d.fFanRpm, "nvmlDeviceGetFanSpeedRPM");
    if (!ok) {
        qWarning() << "[KrakenPump/Fans] NVML: nvml.dll too old (fan control API missing).";
        shutdown();
        return false;
    }
    if (d.fInit() != NVML_SUCCESS) { shutdown(); return false; }
    d.inited = true;

    unsigned count = 0;
    if (d.fCount(&count) != NVML_SUCCESS) { shutdown(); return false; }
    for (unsigned i = 0; i < count; ++i) {
        Gpu g;
        if (d.fHandle(i, &g.dev) != NVML_SUCCESS) continue;
        char nm[96] = {};
        d.fName(g.dev, nm, sizeof(nm));
        g.name = nm[0] ? QString::fromUtf8(nm) : QStringLiteral("NVIDIA GPU");
        if (d.fNumFans(g.dev, &g.fans) != NVML_SUCCESS || g.fans == 0) {
            qInfo() << "[KrakenPump/Fans] NVML:" << g.name << "— no controllable fan, skipped.";
            continue;
        }
        unsigned cur = 0;
        if (d.fFanPct(g.dev, 0, &cur) != NVML_SUCCESS) continue;
        if (d.fMinMax(g.dev, &g.minPct, &g.maxPct) != NVML_SUCCESS || g.maxPct == 0) {
            g.minPct = 0; g.maxPct = 100;
        }
        qInfo().noquote() << QStringLiteral(
            "[KrakenPump/Fans] NVML: %1 — %2 fan(s), allowed speed %3..%4 %, now %5 %, RPM API %6")
            .arg(g.name).arg(g.fans).arg(g.minPct).arg(g.maxPct).arg(cur)
            .arg(d.fFanRpm ? QStringLiteral("yes") : QStringLiteral("no"));
        d.gpus.append(g);
    }
    if (d.gpus.isEmpty()) { shutdown(); return false; }

    if (QFile::exists(Impl::markerPath())) {
        bool allOk = true;
        for (Gpu& g : d.gpus) allOk = d.restoreAuto(g) && allOk;
        qInfo() << "[KrakenPump/Fans] NVML:"
                << (allOk ? "automatic fan policy restored after an unclean exit."
                          : "could not restore automatic fan policy after an unclean exit.");
        if (allOk) QFile::remove(Impl::markerPath());
    }

    m_channels.clear();
    for (int i = 0; i < d.gpus.size(); ++i) {
        NvmlChannel ch;
        ch.id    = QStringLiteral("nvml/%1/control").arg(i);
        ch.rpmId = QStringLiteral("nvml/%1/fan").arg(i);
        ch.name  = QStringLiteral("GPU Fan");
        ch.hw    = d.gpus[i].name;
        m_channels.append(ch);
    }
    return true;
#else
    return false;
#endif
}

void NvmlFan::shutdown()
{
#ifdef _WIN32
    for (int i = 0; i < m_d->gpus.size(); ++i)
        if (m_d->gpus[i].touched)
            setDefault(QStringLiteral("nvml/%1/control").arg(i));
    m_d->gpus.clear();
    m_channels.clear();
    if (m_d->inited && m_d->fShutdown) m_d->fShutdown();
    m_d->inited = false;
    if (m_d->dll) { FreeLibrary(m_d->dll); m_d->dll = nullptr; }
#endif
}

float NvmlFan::value(const QString& id) const
{
    bool isFan = false;
    Gpu* g = m_d->find(id, &isFan);
    if (!g || !m_d->inited) return 0.f;
    if (!isFan) {
        if (g->lastDuty >= 0) return float(g->lastDuty);
        unsigned pct = 0;
        return m_d->fFanPct(g->dev, 0, &pct) == NVML_SUCCESS ? float(pct) : 0.f;
    }
    if (g->touched) m_d->apply(*g);
    if (m_d->fFanRpm) {
        nvmlFanSpeedInfo_t info{ unsigned(sizeof(nvmlFanSpeedInfo_t)) | (1u << 24), 0, 0 };
        if (m_d->fFanRpm(g->dev, &info) == NVML_SUCCESS)
            return float(info.speed);
    }
    if (!g->rpmLogged) {
        qInfo() << "[KrakenPump/Fans] NVML:" << g->name
                << "— RPM not available, fan RPM shown as 0.";
        g->rpmLogged = true;
    }
    return 0.f;
}

bool NvmlFan::setDuty(const QString& id, int percent)
{
    Gpu* g = m_d->find(id);
    if (!g || !m_d->inited) return false;

    if (!g->touched) {
        if (!gpuFanStateWrite(Impl::markerPath(), QByteArray("manual\n")))
            qWarning() << "[KrakenPump/Fans] NVML: cannot write recovery marker"
                       << Impl::markerPath();
        g->touched = true;
    }
    g->desired = std::clamp(percent, int(g->minPct), int(g->maxPct));
    g->lastDuty = percent;
    return m_d->apply(*g);
}

void NvmlFan::setDefault(const QString& id)
{
    Gpu* g = m_d->find(id);
    if (!g || !g->touched || !m_d->inited) return;
    if (m_d->restoreAuto(*g)) {
        g->touched = false;
        g->desired = g->applied = -1;
        g->floorLevel = 0;
        g->lastDuty = -1;
        const bool others = std::any_of(m_d->gpus.cbegin(), m_d->gpus.cend(),
                                        [](const Gpu& x) { return x.touched; });
        if (!others) QFile::remove(Impl::markerPath());
    } else {
        qWarning() << "[KrakenPump/Fans] NVML: could not restore automatic fan policy for"
                   << g->name;
    }
}

}
