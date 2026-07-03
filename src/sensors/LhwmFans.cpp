#include "LhwmFans.h"
#include <lhwm-cpp-wrapper.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

namespace NZXTKrakenPump {

bool LhwmFans::init()
{
    // Même garde que le plugin LCD : la lib statique C++/CLI charge l'assembly
    // .NET au premier appel — s'il est absent le process crashe. On vérifie
    // donc la présence du fichier avant tout appel LHWM::.
    const QString dll = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/lhwm-wrapper.dll");
    if (!QFileInfo::exists(dll)) {
        qWarning() << "[KrakenPump/Fans] lhwm-wrapper.dll not found in"
                   << QCoreApplication::applicationDirPath()
                   << "— motherboard/GPU fan control disabled.";
        return false;
    }

    // Ré-appelable : au boot le driver kernel LHM peut ne pas être encore prêt
    // (GetHardwareSensorMap renvoie 0 Control) — le worker relance init() ;
    // on repart donc d'une énumération propre.
    m_channels.clear();
    m_temps.clear();
    m_ready = false;

    std::map<std::string, std::vector<std::tuple<std::string, std::string, std::string>>> map;
    try {
        map = LHWM::GetHardwareSensorMap();
    } catch (...) {
        qWarning() << "[KrakenPump/Sensors] LHWM::GetHardwareSensorMap threw an "
                      "exception (lhwm-wrapper.dll present but .NET/driver failing?).";
        return false;
    }

    struct RawSensor { QString name, type, id, hw; };
    QVector<RawSensor> all;
    for (const auto& hw : map) {
        // Clé hardware "Nom : /identifiant" (cf. LHWM.cs) -> ne garder que le nom
        QString hwName = QString::fromStdString(hw.first);
        const int sep = hwName.indexOf(QStringLiteral(" : "));
        if (sep > 0) hwName = hwName.left(sep);
        for (const auto& s : hw.second) {
            // Dump diagnostic de TOUS les capteurs (niveau Info) — parité avec le
            // plugin LCD : sert à valider/affiner le mapping ventilos/sondes.
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

    // Canaux pilotables : sensors "Control". RPM associé = même index
    // ("/control/N" -> "/fan/N" sur le même hardware).
    for (const auto& s : all) {
        if (s.type != QLatin1String("Control"))
            continue;
        // GPU exclu : sur les GPU AMD récents (RDNA3/4) LHM passe par ADL qui
        // renvoie ADL_ERR_NOT_SUPPORTED — l'écriture est un no-op silencieux.
        if (s.id.startsWith(QStringLiteral("/gpu")))
            continue;
        LhwmChannel ch;
        ch.id   = s.id;
        ch.name = s.name;
        ch.hw   = s.hw;
        // Nom court : "Nuvoton NCT6799D Fan #1" -> "Fan #1" (le chip Super I/O
        // n'apporte rien à l'utilisateur, le hardware parent est dans ch.hw)
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

    // Sources de température pour les courbes : liste curée à noms simples.
    // On sélectionne la meilleure sonde LHM disponible pour chaque entrée.
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

    // GPU : "GPU Core" = moyenne, "GPU Hot Spot" = point chaud
    add(QStringLiteral("GPU Average"), findTemp([&](const RawSensor& s) {
        return isGpu(s) && s.name == QLatin1String("GPU Core"); }));
    add(QStringLiteral("GPU Hot Spot"), findTemp([&](const RawSensor& s) {
        return isGpu(s) && s.name.contains(QStringLiteral("Hot Spot")); }));

    // CPU : AMD -> CCD (Tdie) = moyenne, Tctl = point chaud ;
    //       Intel -> Package = moyenne, Core Max = point chaud.
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

    // RAM (rare : nécessite des DIMMs avec sonde exposée par LHM)
    add(QStringLiteral("RAM"), findTemp([](const RawSensor& s) {
        return s.id.startsWith(QStringLiteral("/ram")); }));

    // Carte mère : sonde du Super I/O ("System" de préférence)
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
    if (!m_ready || id.isEmpty()) return 0.f;
    return LHWM::GetSensorValue(id.toStdString());
}

void LhwmFans::setDuty(const QString& id, int percent)
{
    if (!m_ready || id.isEmpty()) return;
    LHWM::SetControlValue(id.toStdString(),
                          float(std::clamp(percent, 0, 100)));
}

void LhwmFans::setDefault(const QString& id)
{
    if (!m_ready || id.isEmpty()) return;
    LHWM::SetControlDefault(id.toStdString());
}

} // namespace NZXTKrakenPump
