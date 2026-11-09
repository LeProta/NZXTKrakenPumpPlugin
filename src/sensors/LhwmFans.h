#pragma once
#include <QString>
#include <QVector>
#include "AdlxFan.h"
#include "NvmlFan.h"

namespace NZXTKrakenPump {

// ─────────────────────────────────────────────────────────────────────────────
// LhwmFans
//   Backend LibreHardwareMonitor (lhwm-cpp-wrapper) : énumère les canaux
//   ventilateurs pilotables (sensors de type "Control" : Super I/O carte mère,
//   GPU) et les sondes de température utilisables comme source de courbe.
//   Toutes les méthodes doivent être appelées depuis le MÊME thread (worker).
//   L'assembly .NET lhwm-wrapper.dll doit être à côté d'OpenRGB.exe (même
//   contrainte que le plugin LCD) ; sinon init() renvoie false et le plugin
//   fonctionne en mode Kraken seul.
// ─────────────────────────────────────────────────────────────────────────────
struct LhwmChannel {
    QString id;      // identifiant LHM du Control (ex. /lpc/nct6799d/0/control/0)
    QString rpmId;   // identifiant du capteur RPM associé (vide si aucun)
    QString name;    // ex. "Fan #1", "GPU Fan"
    QString hw;      // hardware parent, ex. "N7 B650E", "AMD Radeon RX 9070 XT"
};

struct LhwmTemp {
    QString id;      // ex. /amdcpu/0/temperature/2
    QString name;    // ex. "CPU (Tctl/Tdie)"
};

class LhwmFans {
public:
    // Énumération complète (lente : ouvre LHM). false si lhwm-wrapper.dll absente.
    bool init();
    bool ready() const { return m_ready; }

    // Canaux LHM seuls (hors GPU AMD/ADLX et NVIDIA/NVML) : base du retry d'init au boot — les
    // canaux GPU, toujours présents, ne doivent pas l'arrêter.
    int lhwmChannelCount() const { return m_lhwmChannels; }

    const QVector<LhwmChannel>& channels() const { return m_channels; }
    const QVector<LhwmTemp>&    temps()    const { return m_temps; }

    float value(const QString& id) const;          // lecture capteur (temp/RPM/duty)
    bool  setDuty(const QString& id, int percent); // écriture duty 0..100 ; false si échec
    void  setDefault(const QString& id);           // rend la main au BIOS/auto

private:
    bool initLhwm();                 // énumération LibreHardwareMonitor seule

    QVector<LhwmChannel> m_channels;
    QVector<LhwmTemp>    m_temps;
    bool                 m_ready = false;
    int                  m_lhwmChannels = 0;
    mutable NvmlFan      m_nvml;     // GPU NVIDIA via NVML (ids "nvml/…")
    mutable AdlxFan      m_adlx;     // GPU AMD via ADLX (ids "adlx/…")
};

} // namespace NZXTKrakenPump
