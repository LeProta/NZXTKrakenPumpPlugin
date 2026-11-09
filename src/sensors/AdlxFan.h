#pragma once
#include <QString>
#include <QVector>
#include <memory>

namespace NZXTKrakenPump {

// ─────────────────────────────────────────────────────────────────────────────
// AdlxFan
//   Ventilateurs des GPU AMD via ADLX (SDK officiel AMD, appels natifs).
//   Remplace le chemin ADL de LibreHardwareMonitor, rejeté sur RDNA3/RDNA4.
//   ADLX n'expose pas un duty direct mais une table (température -> vitesse) :
//   un duty fixe est écrit comme table plate. Le plugin ne remet JAMAIS la GPU
//   à l'usine (ResetToFactory efface aussi l'overclocking Adrenalin) : si ADLX
//   demande un reset, l'écriture échoue et est journalisée.
//   Mêmes contraintes de thread que LhwmFans (un seul thread : le worker).
// ─────────────────────────────────────────────────────────────────────────────
struct AdlxChannel {
    QString id;      // "adlx/<n>/control" (duty) — écriture
    QString rpmId;   // "adlx/<n>/fan"     (RPM)  — lecture
    QString name;    // "GPU Fan"
    QString hw;      // nom de la carte, ex. "AMD Radeon RX 9070 XT"
};

class AdlxFan {
public:
    AdlxFan();
    ~AdlxFan();

    // Idempotent. true si au moins une GPU AMD supporte le réglage manuel.
    bool init();
    void shutdown();                       // rend toutes les GPU puis libère ADLX
    const QVector<AdlxChannel>& channels() const { return m_channels; }

    static bool owns(const QString& id) { return id.startsWith(QLatin1String("adlx/")); }
    float value(const QString& id) const;           // RPM (…/fan) ou dernier duty (…/control)
    bool  setDuty(const QString& id, int percent);  // table plate ; false si refusé
    void  setDefault(const QString& id);            // restaure l'état d'avant le plugin

private:
    struct Impl;
    std::unique_ptr<Impl> m_d;
    QVector<AdlxChannel>  m_channels;
};

} // namespace NZXTKrakenPump
