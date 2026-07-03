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

class KrakenPumpDevice;   // emprunté (possédé par le widget)

// Modes d'un canal (combo UI, persisté par index — config v3).
// Plus de mode "Auto" : tous les canaux sont pilotés par le plugin ; le
// retour BIOS ne se fait qu'au déchargement (stop).
enum FanMode {
    FanSilent = 0,
    FanPerformance,
    FanFixed,          // duty constant (2 points liés à 0° et 100°)
    FanCustom,
};

// Config d'un canal ventilateur (UI -> worker, queued).
// `points` = courbe EFFECTIVE affichée (preset, ou custom) — le worker ne
// connaît pas les presets, il interpole simplement dessus.
struct FanChannelConfig {
    int             mode = FanSilent;
    QString         sourceId;             // "liquid" ou identifiant temp LHM
    QVector<QPoint> points;               // courbe Custom (°C, %) triée
    int             fixedDuty = 50;       // duty du mode Fixed — UI seulement
    QString         name;                 // nom personnalisé (vide = nom LHM) — UI seulement
    int             riseRate = 5;         // montée max en %/s
    int             fallRate = 5;         // descente max en %/s
    int             startDuty = 0;        // duty min de rotation mesuré (calib) ;
                                          // 0 = non calibré (plancher global 5 %).
                                          // Dérivé, non persisté (vient de "calib").
};

// Identifiants réservés des canaux Kraken. Pilotés en HID direct par la
// boucle logicielle (courbe plate ré-écrite à chaque changement de duty) :
// permet n'importe quelle source de température, et contourne le quirk
// Elite V2 (le device se remet parfois seul à 0 % — cf. LHM KrakenV3.cs).
inline const QString KRAKEN_FAN_ID  = QStringLiteral("kraken/fan");
inline const QString KRAKEN_PUMP_ID = QStringLiteral("kraken/pump");

// ─────────────────────────────────────────────────────────────────────────────
// PumpWorker
//   Vit sur son propre QThread. Toute l'I/O passe ici : HID Kraken (open,
//   courbes pompe, statut) + LibreHardwareMonitor (ventilos carte mère/GPU).
//   Boucle ventilos : 1 tick/s, duty = interpolation(courbe, temp source),
//   écrit uniquement quand le duty calculé change. Au stop(), les canaux LHM
//   touchés sont rendus au BIOS (SetControlDefault).
// ─────────────────────────────────────────────────────────────────────────────
class PumpWorker : public QObject {
    Q_OBJECT
public:
    explicit PumpWorker(KrakenPumpDevice* device);

public slots:
    void start();                  // QueuedConnection depuis QThread::started
    void stop();                   // BlockingQueuedConnection depuis le destructeur du widget
    // Config d'un canal (LHM, kraken/fan ou kraken/pump).
    void setFanConfig(QString id, NZXTKrakenPump::FanChannelConfig cfg);
    // "Identify" : pousse le canal à 100 % pendant 5 s puis restaure.
    void identify(QString id);
    // Calibration : balayage 100 % -> 0 % par paliers de 10, RPM échantillonné
    // à chaque palier une fois stabilisé. Le RPM à 100 % devient la référence
    // du % réel affiché ; un canal dont le RPM ne suit pas le duty est masqué
    // (tach externe, ex. tach pompe sur CPU_FAN).
    void calibrate();

public:
    // Headers vides mémorisés (persistés par le widget) — à appeler AVANT le
    // démarrage du thread : masqués dès l'énumération, surveillance continue.
    void presetDeadChannels(const QStringList& ids);
    // Idem pour les headers identifiés comme tach de la pompe Kraken.
    void presetPumpTachChannels(const QStringList& ids);

signals:
    void deviceOpened(const QString& name, bool ok, bool hasFan);
    void statusText(const QString& text);
    void readings(float liquidTemp, int pumpRpm, int pumpDuty, int fanRpm, int fanDuty);

    // Canaux ventilateurs disponibles (ré-émis quand le Kraken (dis)paraît).
    // groups = hardware parent de chaque canal (section UI).
    void fanChannelsReady(const QStringList& ids, const QStringList& names,
                          const QStringList& groups);
    // Sources de température (une fois, après init LHM)
    void tempSourcesReady(const QStringList& ids, const QStringList& names);
    // RPM + duty appliqué par canal (ordre = fanChannelsReady)
    void fanReadings(const QStringList& ids, const QVector<int>& rpms,
                     const QVector<int>& duties);
    // Valeurs des sources de température (ordre = tempSourcesReady)
    void tempValues(const QStringList& ids, const QVector<float>& values);
    // Liste courante des headers vides masqués (persistée par le widget)
    void deadChannelsChanged(const QStringList& ids);
    // Headers dont le tachymètre est celui de la pompe Kraken (persistés)
    void pumpTachChannelsChanged(const QStringList& ids);
    // Calibration : avancement (0-100) puis résultats (courbe duty -> RPM
    // par canal, duties croissants)
    void calibrationProgress(int percent);
    void calibrationFinished(const QMap<QString, QVector<QPoint>>& tables);

private slots:
    void tick();

private:
    bool tryOpen();
    void fanTick();
    void emitFanChannels();
    void finishCalibration();
    float sourceTemp(const QString& id) const;

    KrakenPumpDevice* m_device = nullptr;   // emprunté
    QTimer*           m_timer  = nullptr;

    int           m_failCount     = 0;
    int           m_reopenDelayMs = 3000;
    QElapsedTimer m_reopenClock;

    // ── Ventilateurs ────────────────────────────────────────────────────────
    LhwmFans m_lhwm;
    QMap<QString, FanChannelConfig> m_fanCfg;
    QMap<QString, int>              m_lastDuty;   // dernier duty écrit (base des rampes)
    QSet<QString>                   m_forceWrite; // ré-écriture requise (quirk Elite V2)
                                                  // SANS perdre la base des rampes
    QSet<QString>                   m_managed;    // canaux LHM à restaurer au stop
    QMap<QString, float>            m_smoothTemp;    // EMA par canal (anti-oscillation)
    QMap<QString, qint64>           m_identifyUntil; // id -> échéance ms (m_upClock)
    QElapsedTimer                   m_upClock;       // horloge monotone du worker

    // Headers carte mère vides : duty envoyé mais 0 RPM en continu (pas de fil
    // tachymètre branché) -> masqués de l'UI, ré-affichés si un RPM apparaît.
    QMap<QString, int>              m_zeroRpmStreak; // secondes consécutives à 0 RPM
    QSet<QString>                   m_deadChannels;

    // Headers dont le tachymètre est en fait celui de la pompe Kraken (câble
    // tach des AIO sur CPU_FAN) : RPM collé au RPM pompe en continu -> masqués
    // et rendus au BIOS (leur PWM ne pilote rien). Divergence -> ré-affichés.
    QSet<QString>                   m_pumpTachChannels;
    QMap<QString, int>              m_pumpTachStreak;  // s consécutives de RPM ≈ pompe
    QMap<QString, int>              m_pumpTachDiverge; // s consécutives de divergence
    int                             m_pumpRpm = 0;     // dernier RPM pompe (statut)

    // Calibration en cours : balayage descendant (index dans la liste des
    // paliers) ; RPM échantillonné quand tous les canaux ont atteint un
    // plateau (le ventilo Kraken suit une rampe interne lente), plafond dur
    // par palier.
    bool                            m_calibActive = false;
    int                             m_calibStep   = 0;   // index du palier courant
    int                             m_calibTicks  = 0;   // ticks écoulés (palier)
    int                             m_calibStable = 0;   // ticks stables consécutifs
    QMap<QString, int>              m_calibPrevRpm;
    QMap<QString, QVector<QPoint>>  m_calibTable;        // (duty, rpm) mesurés
    QStringList m_srcIds;                          // sources temp ("liquid" + LHM)
    float       m_liquidTemp   = 0.f;
    int         m_krakenFanRpm = 0;
    int         m_krakenFanDuty = 0;
    bool        m_krakenHasFan = false;
};

} // namespace NZXTKrakenPump

Q_DECLARE_METATYPE(NZXTKrakenPump::FanChannelConfig)
