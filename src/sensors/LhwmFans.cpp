#include "LhwmFans.h"
#include <lhwm-cpp-wrapper.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NZXTKrakenPump {

#ifdef _WIN32
static void pinGpuDriverModules()
{
    static const wchar_t* kModules[] = {
        L"amdxn64.dll", L"amdxx64.dll", L"atiadlxx.dll", L"aticfx64.dll",
        L"nvoglv64.dll", L"nvapi64.dll", L"igd10iumd64.dll",
        L"d3d9.dll", L"d3d11.dll", L"dxgi.dll",
    };
    static bool pinned[sizeof(kModules) / sizeof(kModules[0])] = {};

    for (size_t i = 0; i < sizeof(kModules) / sizeof(kModules[0]); ++i) {
        if (pinned[i]) continue;
        HMODULE h = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, kModules[i], &h))
            pinned[i] = true;
    }
}
#else
static void pinGpuDriverModules() {}
#endif

bool LhwmFans::init()
{
    m_channels.clear();
    const bool lhwmOk = initLhwm();
    m_lhwmChannels = m_channels.size();
    const bool gpuOk = m_adlx.init();
    for (const auto& a : m_adlx.channels())
        m_channels.append({ a.id, a.rpmId, a.name, a.hw });
    const bool nvOk = m_nvml.init();
    for (const auto& a : m_nvml.channels())
        m_channels.append({ a.id, a.rpmId, a.name, a.hw });
    return lhwmOk || gpuOk || nvOk;
}

bool LhwmFans::initLhwm()
{
    const QString dll = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/lhwm-wrapper.dll");
    if (!QFileInfo::exists(dll)) {
        qWarning() << "[KrakenPump/Fans] lhwm-wrapper.dll not found in"
                   << QCoreApplication::applicationDirPath()
                   << "— motherboard/GPU fan control disabled.";
        return false;
    }

    m_channels.clear();
    m_temps.clear();
    m_ready = false;

    std::map<std::string, std::vector<std::tuple<std::string, std::string, std::string>>> map;
    try {
        map = LHWM::GetHardwareSensorMap();
        pinGpuDriverModules();
    } catch (...) {
        qWarning() << "[KrakenPump/Sensors] LHWM::GetHardwareSensorMap threw an "
                      "exception (lhwm-wrapper.dll present but .NET/driver failing?).";
        return false;
    }

    struct RawSensor { QString name, type, id, hw; };
    QVector<RawSensor> all;
    for (const auto& hw : map) {
        QString hwName = QString::fromStdString(hw.first);
        const int sep = hwName.indexOf(QStringLiteral(" : "));
        if (sep > 0) hwName = hwName.left(sep);
        for (const auto& s : hw.second) {
            qInfo().noquote()
                << "[KrakenPump/Sensors] LHM" << QString::fromStdString(hw.first)
                << "|"  << QString::fromStdString(std::get<1>(s))
                << "|"  << QString::fromStdString(std::get<0>(s))
                << "->" << QString::fromStdString(std::get<2>(s));
            all.append({ QString::fromStdString(std::get<0>(s)),
                         QString::fromStdString(std::get<1>(s)),
                         QString::fromStdString(std::get<2>(s)),
                         hwName });
        }
    }

    if (all.isEmpty()) {
        qWarning() << "[KrakenPump/Sensors] LHWM: empty sensor map. DLL present but "
                      "no sensors — Memory Integrity (HVCI) blocking the driver, "
                      "or OpenRGB not running as administrator?";
        return false;
    }

    for (const auto& s : all) {
        if (s.type != QLatin1String("Control"))
            continue;
        if (s.id.startsWith(QStringLiteral("/gpu")))
            continue;
        LhwmChannel ch;
        ch.id   = s.id;
        ch.name = s.name;
        ch.hw   = s.hw;
        const int fanPos = ch.name.indexOf(QStringLiteral("Fan"));
        if (fanPos > 0)
            ch.name = ch.name.mid(fanPos);
        QString rpmId = s.id;
        rpmId.replace(QStringLiteral("/control/"), QStringLiteral("/fan/"));
        for (const auto& f : all)
            if (f.type == QLatin1String("Fan") && f.id == rpmId) {
                ch.rpmId = rpmId;
                break;
            }
        m_channels.append(ch);
    }

    const auto findTemp = [&all](auto pred) -> QString {
        for (const auto& s : all)
            if (s.type == QLatin1String("Temperature") && pred(s))
                return s.id;
        return QString();
    };
    const auto add = [this](const QString& name, const QString& id) {
        if (!id.isEmpty())
            m_temps.append({ id, name });
    };

    const auto isCpu = [](const RawSensor& s) {
        return s.id.startsWith(QStringLiteral("/amdcpu"))
            || s.id.startsWith(QStringLiteral("/intelcpu"));
    };
    const auto isGpu = [](const RawSensor& s) {
        return s.id.startsWith(QStringLiteral("/gpu"));
    };

    add(QStringLiteral("GPU Average"), findTemp([&](const RawSensor& s) {
        return isGpu(s) && s.name == QLatin1String("GPU Core"); }));
    add(QStringLiteral("GPU Hot Spot"), findTemp([&](const RawSensor& s) {
        return isGpu(s) && s.name.contains(QStringLiteral("Hot Spot")); }));

    QString cpuAvg = findTemp([&](const RawSensor& s) {
        return isCpu(s) && s.name.contains(QStringLiteral("CCD")); });
    if (cpuAvg.isEmpty())
        cpuAvg = findTemp([&](const RawSensor& s) {
            return isCpu(s) && s.name.contains(QStringLiteral("Package")); });
    QString cpuHot = findTemp([&](const RawSensor& s) {
        return isCpu(s) && s.name.contains(QStringLiteral("Tctl")); });
    if (cpuHot.isEmpty())
        cpuHot = findTemp([&](const RawSensor& s) {
            return isCpu(s) && s.name.contains(QStringLiteral("Core Max")); });
    if (cpuAvg.isEmpty()) cpuAvg = cpuHot;
    if (cpuHot.isEmpty()) cpuHot = cpuAvg;
    add(QStringLiteral("CPU Average"), cpuAvg);
    add(QStringLiteral("CPU Hot Spot"), cpuHot);

    add(QStringLiteral("RAM"), findTemp([](const RawSensor& s) {
        return s.id.startsWith(QStringLiteral("/ram")); }));

    QString mb = findTemp([](const RawSensor& s) {
        return s.id.contains(QStringLiteral("/lpc/"))
            && s.name.contains(QStringLiteral("System")); });
    if (mb.isEmpty())
        mb = findTemp([](const RawSensor& s) {
            return s.id.contains(QStringLiteral("/lpc/")); });
    add(QStringLiteral("Motherboard"), mb);

    m_ready = true;
    qInfo() << "[KrakenPump/Fans] LHM ready:" << m_channels.size()
            << "fan control channel(s)," << m_temps.size() << "temp source(s).";
    for (const auto& ch : m_channels)
        qInfo() << "[KrakenPump/Fans]  control:" << ch.name << ch.id
                << (ch.rpmId.isEmpty() ? "(no RPM)" : "");
    return true;
}

float LhwmFans::value(const QString& id) const
{
    if (AdlxFan::owns(id)) return m_adlx.value(id);
    if (NvmlFan::owns(id)) return m_nvml.value(id);
    if (!m_ready || id.isEmpty()) return 0.f;
    pinGpuDriverModules();
    try { return LHWM::GetSensorValue(id.toStdString()); }
    catch (...) { return 0.f; }
}

bool LhwmFans::setDuty(const QString& id, int percent)
{
    if (AdlxFan::owns(id)) return m_adlx.setDuty(id, std::clamp(percent, 0, 100));
    if (NvmlFan::owns(id)) return m_nvml.setDuty(id, std::clamp(percent, 0, 100));
    if (!m_ready || id.isEmpty()) return false;
    try {
        LHWM::SetControlValue(id.toStdString(),
                              float(std::clamp(percent, 0, 100)));
        return true;
    } catch (...) {
        return false;
    }
}

void LhwmFans::setDefault(const QString& id)
{
    if (AdlxFan::owns(id)) { m_adlx.setDefault(id); return; }
    if (NvmlFan::owns(id)) { m_nvml.setDefault(id); return; }
    if (!m_ready || id.isEmpty()) return;
    try { LHWM::SetControlDefault(id.toStdString()); }
    catch (...) {}
}

}
