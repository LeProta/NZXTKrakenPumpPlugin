#include "PumpWorker.h"
#include "../device/KrakenPumpDevice.h"
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace NZXTKrakenPump {

// Interpolation linéaire par morceaux sur les points (°C, %) — plateau avant
// le premier point et après le dernier (même sémantique que l'éditeur).
static int interpDuty(const QVector<QPoint>& pts, float t)
{
    if (pts.isEmpty()) return 50;
    if (t <= pts.first().x()) return pts.first().y();
    if (t >= pts.last().x())  return pts.last().y();
    for (int k = 1; k < pts.size(); ++k) {
        if (t <= pts[k].x()) {
            const QPoint a = pts[k - 1], b = pts[k];
            if (a.x() == b.x())   // marche verticale (points alignés par poussée)
                return b.y();
            return int(std::lround(a.y() + double(b.y() - a.y())
                       * (t - a.x()) / double(b.x() - a.x())));
        }
    }
    return pts.last().y();
}

// Plage de validité d'une sonde (même règle que le plugin LCD)
static inline bool tempValid(float t) { return t > 0.f && t < 150.f; }

// Sonde invalide pendant ce nombre de ticks (s) consécutifs -> canal rendu au
// BIOS + alerte UI. Marge pour l'init lente du driver LHM au démarrage.
static constexpr int kFaultTicks = 8;

// Calibration : balayage descendant 100 % -> 0 % par paliers de 10,
// échantillonnage quand les RPM se sont STABILISÉS (le ventilo du Kraken suit
// une rampe interne lente — une durée fixe le mesurait en pleine
// accélération). Plateau = variation ≤ ~3 % pendant 3 ticks, après un délai
// minimum ; plafond dur par palier.
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

    // Ouvrir le Kraken D'ABORD (rapide, HID) : le nom du device s'affiche
    // immédiatement, sans attendre l'init LHM (~2-5 s) — même réactivité que
    // le plugin LCD. À ce stade m_lhwm n'est pas encore prêt : emitFanChannels
    // ne liste que le canal Kraken, complété après l'init ci-dessous.
    const bool krakenOk = tryOpen();
    if (!krakenOk) {
        emit deviceOpened(QString(), false, false);
        // Même consigne que le plugin LCD, plus la spécificité pompe : les
        // canaux carte mère restent pilotables sans Kraken.
        emit statusText(QStringLiteral(
            "Check the USB connection and close NZXT CAM — motherboard fans remain available."));
        m_reopenClock.start();
    }

    // LibreHardwareMonitor : énumération des ventilos carte mère/GPU.
    // Lent (~2-5 s, ouvre LHM + driver kernel) — on est sur le thread worker.
    m_lhwm.init();
    publishLhwmSources();

    // Course driver : si l'énumération n'a produit aucun canal pilotable, le
    // driver kernel LHM n'était probablement pas prêt (fréquent juste après
    // l'ouverture de session). On programme des ré-init périodiques.
    if (m_lhwm.channels().isEmpty()) {
        m_lhwmRetriesLeft = 8;    // ~ jusqu'à 64 s (8 s/essai)
        m_lhwmRetryTick   = 8;
        qWarning() << "[KrakenPump/Fans] no fan channels at startup — "
                      "will retry LHM init (driver may not be ready yet).";
    }

    m_timer->start();
}

void PumpWorker::publishLhwmSources()
{
    // Sources de température : liquide Kraken + sondes LHM (CPU/GPU).
    // Reconstruit m_srcIds à zéro (appelable après une ré-init LHM).
    m_srcIds.clear();
    QStringList srcNames;
    m_srcIds << QStringLiteral("liquid");
    srcNames << QStringLiteral("Liquid");
    for (const auto& t : m_lhwm.temps()) {
        m_srcIds << t.id;
        srcNames << t.name;
    }
    emit tempSourcesReady(m_srcIds, srcNames);
    emitFanChannels();   // re-liste avec les canaux LHM (carte mère/GPU)
}

void PumpWorker::stop()
{
    if (m_timer) { m_timer->stop(); m_timer = nullptr; }

    // Failsafe : rendre au BIOS/auto tous les canaux LHM qu'on a pilotés.
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
            continue;   // tachymètre de la pompe Kraken : masqué (rendu au BIOS)
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
    m_lastDuty.remove(KRAKEN_FAN_ID);    // forcer une ré-écriture après reconnexion
    m_lastDuty.remove(KRAKEN_PUMP_ID);
    emit deviceOpened(m_device->displayName(), true, hasFan);
    // Pas de "Connected" transitoire (comme le plugin LCD) : la ligne de statut
    // passe directement aux relevés live au premier tick.
    emitFanChannels();
    return true;
}

void PumpWorker::setFanConfig(QString id, FanChannelConfig cfg)
{
    m_fanCfg[id] = cfg;
    m_lastDuty.remove(id);     // forcer une écriture au prochain tick
    m_smoothTemp.remove(id);   // la sonde a pu changer : repartir de la valeur
                               // brute, pas de l'EMA de l'ancienne sonde
}

void PumpWorker::presetPumpTachChannels(const QStringList& ids)
{
    for (const QString& id : ids)
        m_pumpTachChannels.insert(id);
}

void PumpWorker::releaseChannel(QString id)
{
    // L'UI a masqué ce canal : on cesse de le piloter. m_fanCfg vidé -> la
    // boucle de courbe ne l'itère plus ; s'il était piloté (LHM), on le rend
    // au BIOS. Réversible : « Show » repousse la config (setFanConfig).
    m_fanCfg.remove(id);
    if (m_managed.remove(id))
        m_lhwm.setDefault(id);
    m_lastDuty.remove(id);
    m_smoothTemp.remove(id);
    m_forceWrite.remove(id);
    m_srcBadStreak.remove(id);   // sinon un Show re-fauterait au 1er tick invalide
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
        return;   // 100 % forcé fausserait l'échantillonnage RPM du palier
    if (!m_upClock.isValid()) m_upClock.start();
    m_identifyUntil[id] = m_upClock.elapsed() + 5000;
    m_lastDuty.remove(id);   // écrire 100 % dès le prochain tick
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
    // Valeurs des sources de température (curseur live de l'éditeur).
    // Lues UNE fois par tick et mises en cache : chaque GetSensorValue LHM
    // déclenche un Hardware.Update() côté .NET — sans cache, 7 canaux sur la
    // même sonde CPU = 7 updates redondants par seconde.
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

    // RPM + duty par canal — et détection des headers vides : duty envoyé
    // (>= 20 % : le preset Silent tourne à ~20 % au repos, il faut pouvoir
    // juger même à froid ; un ventilo zero-RPM à 0 % n'est jamais jugé) mais
    // 0 RPM pendant 15 s consécutives -> canal masqué. Le seuil bas est
    // compensé par la durée : un vrai ventilo calé à 20 % finit par tourner
    // au premier passage >= 30 %, et tout RPM ré-affiche le canal.
    QStringList ids;
    QVector<int> rpms, duties;
    QHash<QString, int> tickRpm;   // RPM lus une fois/tick (réutilisés par la
                                   // calibration — chaque value() = 1 update .NET)
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

        // Header dont le tachymètre est celui de la pompe Kraken (câble tach
        // des AIO sur CPU_FAN) : RPM collé (±2 %) au RPM pompe pendant 15 s
        // (même délai que les headers vides -> comportement homogène) ->
        // masqué et rendu au BIOS (le PWM de ce header ne pilote rien).
        // Divergence prolongée (ventilo réel branché) -> ré-affiché.
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
                    m_lastDuty.remove(ch.id);   // forcer la ré-écriture (le canal
                                                // était rendu au BIOS : reprendre)
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

        // Masquage des prises vides : plus de détection auto par RPM (trop de
        // faux positifs + spin-up). L'utilisateur masque les canaux inutilisés
        // manuellement côté UI (persisté). Le worker liste TOUT (sauf tach
        // pompe), l'UI filtre.
        if (m_pumpTachChannels.contains(ch.id))
            continue;
        ids << ch.id;
        rpms << rpm;
        duties << duty;
    }
    emit fanReadings(ids, rpms, duties);
    if (channelsChanged)
        emitFanChannels();

    // Écriture d'un duty sur un canal (kraken = courbe plate, sinon LHM).
    // Retourne true si l'écriture a réellement eu lieu.
    auto writeDuty = [this](const QString& id, int duty) -> bool {
        if (id == KRAKEN_FAN_ID) {
            if (!m_krakenHasFan || !m_device->isOpen()) return false;
            std::array<uint8_t, 60> flat{};
            flat.fill(static_cast<uint8_t>(duty));
            if (!m_device->setCurve(Channel::Fan, flat)) return false;
        } else if (id == KRAKEN_PUMP_ID) {
            if (!m_device->isOpen()) return false;
            std::array<uint8_t, 60> flat{};
            flat.fill(static_cast<uint8_t>(duty));
            if (!m_device->setCurve(Channel::Pump, flat)) return false;
        } else {
            m_lhwm.setDuty(id, duty);
            m_managed.insert(id);
        }
        m_lastDuty[id] = duty;
        m_forceWrite.remove(id);
        return true;
    };

    if (!m_upClock.isValid()) m_upClock.start();
    const qint64 nowMs = m_upClock.elapsed();

    // ── Calibration : balayage 100 % -> 0 % (pas de 10) sur les ventilos ────
    // RPM échantillonné à chaque palier une fois stabilisé ; le RPM à 100 %
    // devient la référence du % réel affiché, un canal insensible au duty est
    // masqué (tach externe). La pompe n'est pas calibrée : sa courbe tourne.
    if (m_calibActive) {
        // Seuls les canaux CONFIGURÉS (m_fanCfg) sont calibrés : un canal
        // masqué (releaseChannel) n'a pas de config — le balayer le laisserait
        // épinglé au dernier duty (0 %) car la boucle de courbe ne le rendrait
        // jamais au BIOS après la calibration.
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

        if (m_calibSpinStep != m_calibStep) {   // nouveau palier -> anim UI
            m_calibSpinStep = m_calibStep;
            emit calibrationSpin(calibIds, duty);
        }

        // RPM courants (cache du tick) + test de plateau : tous les canaux
        // stables (≤ ~3 %) pendant 3 ticks consécutifs, ou plafond de palier.
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
            // Avancement : palier courant + fraction du plateau en cours.
            const qreal frac = std::min(1.0, m_calibTicks / qreal(CALIB_MIN_TICKS + 3));
            emit calibrationProgress(
                std::min(99, int(100.0 * (m_calibStep + frac) / CALIB_STEPS)));
        }
    }

    // ── Identify : 100 % pendant 2 s, prioritaire sur tout le reste ─────────
    for (auto it = m_identifyUntil.begin(); it != m_identifyUntil.end(); ) {
        const QString id = it.key();
        if (nowMs < it.value()) {
            // Pendant une calibration, ne pas lutter contre ses duties
            if (!m_calibActive && m_lastDuty.value(id, -1) != 100)
                writeDuty(id, 100);
            ++it;
            continue;
        }
        // Fin : la boucle de courbe reprend la main au tick suivant.
        it = m_identifyUntil.erase(it);
        m_lastDuty.remove(id);
    }


    // ── Application des courbes (boucle logicielle) ──────────────────────────
    // Canaux dont la sonde est absente ce tick (rendus au BIOS + alerte UI).
    QStringList faultIds, faultReasons;
    const auto faultReason = [this](const QString& sid) -> QString {
        if (sid.isEmpty())
            return QStringLiteral("No temperature source selected.");
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
            const int li = m_lastFaultIds.indexOf(id);   // conserver l'alerte
            if (li >= 0) { faultIds << id; faultReasons << m_lastFaultReasons.value(li); }
            continue;
        }
        if (m_pumpTachChannels.contains(id))
            continue;   // header = tach pompe, PWM rendu au BIOS
        if (m_calibActive && id != KRAKEN_PUMP_ID)
            continue;   // calibration en cours : elle tient les duties

        // Température lissée par EMA (anti-oscillation quand la temp vibre
        // autour d'un point de courbe). Une sonde morte (0 °C LHM, liquide
        // figé après déconnexion) n'est jamais utilisée pour piloter.
        const auto cached = tickTemp.constFind(cfg.sourceId);
        const float raw = (cached != tickTemp.constEnd()) ? cached.value()
                                                          : sourceTemp(cfg.sourceId);
        if (!tempValid(raw)) {
            m_smoothTemp.remove(id);
            // "liquid" exclu : connectivité device signalée via statusText.
            if (cfg.sourceId != QLatin1String("liquid")) {
                const int n = m_srcBadStreak.value(id, 0) + 1;
                m_srcBadStreak.insert(id, n);
                if (n >= kFaultTicks) {
                    faultIds << id;
                    faultReasons << faultReason(cfg.sourceId);
                    if (m_managed.remove(id)) {   // rendre au BIOS une seule fois
                        m_lhwm.setDefault(id);
                        m_lastDuty.remove(id);
                        m_forceWrite.remove(id);
                    }
                }
            }
            continue;
        }
        m_srcBadStreak.remove(id);   // sonde revenue : reset streak
        float t;
        auto st = m_smoothTemp.find(id);
        if (st == m_smoothTemp.end()) {
            m_smoothTemp.insert(id, raw);
            t = raw;
        } else {
            st.value() += 0.4f * (raw - st.value());   // EMA, ~5 s de réponse
            t = st.value();
        }

        int target = std::clamp(interpDuty(cfg.points, t), 0, 100);

        const bool isPump = (id == KRAKEN_PUMP_ID);
        const int  last   = m_lastDuty.value(id, -1);

        // Seuil zero-RPM : sous ce duty le ventilo est coupé (0 %). Seul seuil
        // manuel (cfg.zeroBelow) ; sinon seuil mesuré à la calibration ; sinon
        // AUCUN plancher (0) — la courbe est suivie telle quelle, l'utilisateur
        // choisit lui-même son point de coupure.
        const int dutyFloor = (cfg.zeroBelow > 0) ? cfg.zeroBelow
                            : (cfg.startDuty > 0) ? cfg.startDuty : 0;
        if (isPump) {
            target = std::max(target, 20);   // plancher sécurité pompe
        } else if (target > 0 && target < dutyFloor) {
            target = 0;
        }

        // Rampes par canal : montée/descente plafonnées en %/s (tick = 1 s)
        if (last >= 0) {
            if (target > last)
                target = std::min(target, last + std::max(1, cfg.riseRate));
            else if (target < last)
                target = std::max(target, last - std::max(1, cfg.fallRate));
        }

        // Ré-écriture forcée (quirk Elite V2) : renvoyer même un duty inchangé
        const bool force = m_forceWrite.contains(id);
        if (!force) {
            if (last == target)
                continue;
            // Deadband ±1 % : ignore les variations résiduelles (hors extrêmes)
            if (last >= 0 && std::abs(target - last) <= 1
                    && target != 0 && target != 100)
                continue;
        }

        writeDuty(id, target);
    }

    if (faultIds != m_lastFaultIds) {   // émettre seulement au changement
        m_lastFaultIds = faultIds;
        m_lastFaultReasons = faultReasons;
        emit fanSensorFault(faultIds, faultReasons);
    }
}

// Fin de calibration : chaque canal a une table (duty, rpm) sur tout le
// balayage. RPM insensible au duty (100 % ≈ 0 %) = tachymètre externe (pompe)
// -> masqué + rendu au BIOS. Sinon la table est émise au widget (base du %
// réel affiché : % = interp inverse RPM->duty).
void PumpWorker::finishCalibration()
{
    m_calibActive = false;
    QMap<QString, QVector<QPoint>> tables;
    bool tachChanged = false;
    for (auto it = m_calibTable.constBegin(); it != m_calibTable.constEnd(); ++it) {
        const QString&          id  = it.key();
        const QVector<QPoint>&  tbl = it.value();   // (duty, rpm), duty décroissant
        int rpmHigh = 0, rpmLow = 1 << 30;
        for (const QPoint& p : tbl) {
            rpmHigh = std::max(rpmHigh, p.y());
            rpmLow  = std::min(rpmLow,  p.y());
        }
        // Insensible au duty : amplitude RPM < 10 % du max -> tach externe
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
    // Reprise des courbes au tick suivant : m_lastDuty = duty de calibration,
    // les rampes redescendent naturellement.
    emit calibrationSpin(QStringList(), 0);   // arrêt anim pales
    emit calibrationProgress(100);
    emit calibrationFinished(tables);
    qInfo() << "[KrakenPump/Fans] calibration finished —" << tables.size()
            << "fan(s) measured";
}

// Ré-init LHM différée : le driver kernel peut n'être prêt qu'après l'ouverture
// de session. init() est lent (~2-5 s) et bloque ce thread — appelé en FIN de
// tick (après le relevé pompe, déjà émis) et espacé, pour ne pas figer le
// statut. Jamais pendant une calibration.
void PumpWorker::maybeRetryLhwm()
{
    if (m_lhwmRetriesLeft <= 0 || m_calibActive)
        return;
    if (--m_lhwmRetryTick > 0)
        return;
    m_lhwmRetryTick = 8;
    --m_lhwmRetriesLeft;
    m_lhwm.init();
    if (!m_lhwm.channels().isEmpty()) {
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

            // Quirk Elite V2 : le device se remet parfois seul à 0 % après une
            // commande. Si le duty rapporté diverge de ce qu'on a écrit,
            // marquer le canal pour ré-écriture — SANS toucher m_lastDuty,
            // qui sert aussi de base aux rampes et au zero-RPM (l'effacer
            // faisait sauter la limitation de pente sur un simple retard de
            // rapport du device).
            if (m_lastDuty.contains(KRAKEN_PUMP_ID)
                    && r.pumpDuty != m_lastDuty.value(KRAKEN_PUMP_ID))
                m_forceWrite.insert(KRAKEN_PUMP_ID);
            if (m_lastDuty.contains(KRAKEN_FAN_ID)
                    && r.fanDuty != m_lastDuty.value(KRAKEN_FAN_ID))
                m_forceWrite.insert(KRAKEN_FAN_ID);
        } else if (++m_failCount >= 3) {
            // Device décroché (déconnexion, veille…) : fermer et re-sonder
            qWarning() << "[KrakenPump/Worker] status read failed 3× — device lost.";
            m_device->close();
            m_failCount     = 0;
            m_reopenDelayMs = 3000;
            m_reopenClock.start();
            m_krakenHasFan  = false;
            m_liquidTemp    = 0.f;   // invalide la source "liquid" (garde fanTick)
            m_pumpRpm       = 0;     // stoppe la détection tach pompe
            // Calibration en cours : l'abandonner (le ventilo Kraken n'est plus
            // mesurable) et réactiver le bouton — l'utilisateur relancera.
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
        maybeRetryLhwm();   // en fin de tick : le statut pompe est déjà émis
        return;
    }

    fanTick();   // les ventilos LHM restent pilotés même sans Kraken
    maybeRetryLhwm();

    // Pas de device : sondage throttlé + backoff quand présent mais non ouvrable
    if (m_reopenClock.isValid() && m_reopenClock.elapsed() < m_reopenDelayMs)
        return;
    m_reopenClock.start();

    if (!KrakenPumpDevice::anySupportedPresent()) {
        m_reopenDelayMs = 3000;   // absent : re-sonder tranquillement
        return;
    }
    if (!tryOpen())
        m_reopenDelayMs = std::min(m_reopenDelayMs * 2, 30000);
}

} // namespace NZXTKrakenPump
