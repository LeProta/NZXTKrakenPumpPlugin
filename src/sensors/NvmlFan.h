#pragma once
#include <QString>
#include <QVector>
#include <memory>

namespace NZXTKrakenPump {

// Palier du plancher thermique (% mini) selon la température GPU, avec hystérésis de
// 3 °C pour ne pas osciller autour d'un seuil : 60 % dès 75 °C, 100 % dès 85 °C.
// `prev` = palier actuel. Écrit à chaque écriture ET à chaque lecture RPM (1 Hz).
// shortcut: seuils fixes ; à rendre réglables si besoin réel.
inline int nvmlFloorLevel(unsigned tempC, int prev)
{
    if (tempC >= 85 || (prev == 100 && tempC >= 82)) return 100;
    if (tempC >= 75 || (prev >= 60 && tempC >= 72)) return 60;
    return 0;
}
// ─────────────────────────────────────────────────────────────────────────────
// NvmlFan
//   Ventilateurs des GPU NVIDIA via NVML (nvml.dll fournie avec le pilote,
//   chargée dynamiquement : aucune dépendance de build, absente = désactivé).
//   NVML est l'API documentée par NVIDIA ; le SDK public NVAPI n'a pas de
//   fonction d'écriture des ventilateurs.
//   nvmlDeviceSetFanSpeed_v2 passe la GPU en politique MANUELLE : plus rien ne
//   la protège si le plugin s'arrête. D'où (1) un plancher thermique appliqué à
//   chaque écriture et (2) la restauration de la politique d'origine
//   (nvmlDeviceSetDefaultFanSpeed_v2) à setDefault() et à shutdown().
//   Droits administrateur requis par NVML pour écrire. Non testé sur matériel
//   NVIDIA réel (développé sur AMD). Même contrainte de thread que LhwmFans.
// ─────────────────────────────────────────────────────────────────────────────
struct NvmlChannel {
    QString id;      // "nvml/<n>/control" (duty) — écriture
    QString rpmId;   // "nvml/<n>/fan"     (RPM)  — lecture
    QString name;    // "GPU Fan"
    QString hw;      // nom de la carte, ex. "NVIDIA GeForce RTX 4080"
};

class NvmlFan {
public:
    NvmlFan();
    ~NvmlFan();

    // Idempotent. true si au moins une GPU NVIDIA expose un ventilateur pilotable.
    bool init();
    void shutdown();                       // rend toutes les GPU puis libère NVML
    const QVector<NvmlChannel>& channels() const { return m_channels; }

    static bool owns(const QString& id) { return id.startsWith(QLatin1String("nvml/")); }
    float value(const QString& id) const;           // RPM (…/fan) ou dernier duty (…/control)
    bool  setDuty(const QString& id, int percent);  // false si refusé
    void  setDefault(const QString& id);            // politique d'origine (auto)

private:
    struct Impl;
    std::unique_ptr<Impl> m_d;
    QVector<NvmlChannel>  m_channels;
};

} // namespace NZXTKrakenPump
