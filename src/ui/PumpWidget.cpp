#include "PumpWidget.h"
#include "PumpCurveEditor.h"
#include "KrakenOpenRGBSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace NZXTKrakenPump {

// Libellé "muet" (statut, RPM) : même style que le plugin LCD.
static QString statusLabelStyle(bool dark)
{
    return QString("color:%1;font-size:11px;font-family:monospace;")
           .arg(dark ? "#aaa" : "#666");
}

// Nom de canal renommable : QLineEdit "invisible" (double-clic pour éditer).
// Police posée via QFont (pas la stylesheet) pour que fontMetrics() soit juste.
static QString nameEditStyle()
{
    return QStringLiteral("QLineEdit{background:transparent;border:none;}");
}

// Largeur ajustée au texte : sans ça le QLineEdit avale toute la rangée et
// intercepte les clics destinés au repli/dépli du panneau.
static void fitNameWidth(QLineEdit* le)
{
    const int w = le->fontMetrics().horizontalAdvance(le->text()) + 18;
    le->setFixedWidth(std::max(w, 40));
}

// Bloc hardware : cadre discret, fond transparent (suit le thème OpenRGB).
static QString groupFrameStyle(bool dark)
{
    return QString("QFrame#groupBlock{border:1px solid %1;border-radius:6px;}")
           .arg(dark ? "#2A2A2E" : "#D0D0D4");
}

static QString separatorStyle(bool dark)
{
    return QString("background:%1;border:none;")
           .arg(dark ? "#2A2A2E" : "#E0E0E4");
}

// Icône carrée 28×28 du type de canal (pompe / ventilateur / GPU).
// `angle` : rotation des pales (animation pendant Identify).
static QPixmap makeChannelIcon(int type, bool dark, qreal angle = 0.0)
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor bg     = dark ? QColor(0x1F, 0x1F, 0x23) : QColor(0xEB, 0xEB, 0xEE);
    const QColor border = dark ? QColor(0x2A, 0x2A, 0x2E) : QColor(0xD0, 0xD0, 0xD4);
    const QColor fg     = dark ? QColor(0xE6, 0xE6, 0xEA) : QColor(0x2A, 0x2A, 0x2E);

    // Silhouette pleine : cadre carré arrondi troué d'un cercle (style visserie
    // aux coins), détails découpés en transparence.
    const auto drawFrame = [&](qreal holeR) {
        QPainterPath frame;
        frame.setFillRule(Qt::OddEvenFill);
        frame.addRoundedRect(QRectF(1, 1, 26, 26), 5, 5);
        frame.addEllipse(QPointF(14, 14), holeR, holeR);
        p.setPen(Qt::NoPen);
        p.fillPath(frame, fg);
    };
    const auto drawScrews = [&]() {
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        for (const QPointF& c : { QPointF(4.4, 4.4),  QPointF(23.6, 4.4),
                                  QPointF(4.4, 23.6), QPointF(23.6, 23.6) })
            p.drawEllipse(c, 1.1, 1.1);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    };

    switch (type) {
    case 0: {   // Pompe : cadre + grande face circulaire pleine (anneau creux)
        drawFrame(10.6);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawEllipse(QPointF(14, 14), 8.6, 8.6);
        drawScrews();
        break;
    }
    case 2: {   // GPU : silhouette de carte (corps + connecteur PCIe + ventilateur)
        p.setBrush(bg);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(QRectF(0.5, 0.5, 27, 27), 5, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawRoundedRect(QRectF(5, 9, 18, 8.5), 1.5, 1.5);   // corps
        p.drawRect(QRectF(7, 17.5, 8, 2.2));                  // connecteur PCIe
        p.setBrush(bg);                                       // ouïe du ventilateur
        p.drawEllipse(QPointF(17.5, 13.2), 2.9, 2.9);
        p.setBrush(fg);
        p.drawEllipse(QPointF(17.5, 13.2), 1.0, 1.0);         // moyeu
        break;
    }
    default: {  // Ventilateur : cadre + 7 pales incurvées + moyeu annulaire
        drawFrame(10.6);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        for (int k = 0; k < 7; ++k) {
            p.save();
            p.translate(14, 14);
            p.rotate(360.0 / 7.0 * k + angle);
            QPainterPath blade;
            blade.moveTo(0.0, -3.0);
            blade.cubicTo(4.6, -4.4, 6.6, -8.0, 4.0, -10.3);
            blade.cubicTo(0.8, -9.2, -0.8, -6.0, -1.4, -3.0);
            blade.closeSubpath();
            p.drawPath(blade);
            p.restore();
        }
        p.drawEllipse(QPointF(14, 14), 3.5, 3.5);   // moyeu
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.drawEllipse(QPointF(14, 14), 1.5, 1.5);   // centre du moyeu (creux)
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        drawScrews();
        break;
    }
    }
    return pm;
}

// Pastille de statut de calibration : rond vert + coche blanche, widget overlay
// posé À CHEVAL sur le coin haut-droit du bouton "Calibrate fans" (chevauche
// le contour, moitié dehors — un badge peint dans le bouton serait rogné par
// son rect). Fond transparent, transparent aux clics (le bouton reçoit tout).
class CornerBadge : public QWidget {
public:
    explicit CornerBadge(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFixedSize(15, 15);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal r = 6.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x2E, 0xCC, 0x71));         // vert
        p.drawEllipse(c, r, r);
        QPen check(QColor(Qt::white), 1.6);
        check.setCapStyle(Qt::RoundCap);
        check.setJoinStyle(Qt::RoundJoin);
        p.setPen(check);
        p.drawPolyline(QPolygonF({ c + QPointF(-2.6, 0.2),
                                   c + QPointF(-0.7, 2.2),
                                   c + QPointF(2.8, -2.2) }));
    }
};

// Coche blanche pour l'indicateur de case à cocher (état coché). Générée une
// fois dans le dossier de config puis référencée par url() dans la stylesheet
// (les data: URIs ne sont pas fiables dans les stylesheets Qt 5). Chemin en
// slashes avant pour Qt CSS.
static QString checkMarkImagePath()
{
    const QString path = OpenRGBSettings::configDir()
                         + QStringLiteral("/NZXTKrakenPump/checkmark.png");
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!QFileInfo::exists(path)) {
        QPixmap pm(15, 15);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(Qt::white), 2.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(QPolygonF({ QPointF(3.2, 8.0), QPointF(6.2, 11.0),
                                   QPointF(11.6, 4.4) }));
        p.end();
        pm.save(path, "PNG");
    }
    return QDir::fromNativeSeparators(path);
}

// ─── Presets (calés sur les profils usuels du marché : CAM/FanControl) ──────
// Pompe : sur température liquide (28-30 °C au repos, ~40 °C en charge)
static QVector<QPoint> pumpPresetPoints(int mode)
{
    switch (mode) {
        default:
        case FanSilent:      return { {25, 50}, {32, 60}, {38, 80}, {44, 100} };
        case FanPerformance: return { {25, 80}, {34, 90}, {40, 100} };
    }
}

// Ventilateurs : sur température CPU/GPU (repos ~40 °C, charge 70-85 °C)
static QVector<QPoint> fanPresetPoints(int fanMode)
{
    switch (fanMode) {
        default:
        case FanSilent:      return { {30, 20}, {50, 35}, {65, 55}, {75, 80}, {82, 100} };
        case FanPerformance: return { {30, 35}, {45, 55}, {60, 80}, {72, 100} };
    }
}

// Mode Fixed : duty constant matérialisé par 2 points liés (0° et 100°)
static QVector<QPoint> fixedPoints(int duty)
{
    return { QPoint(0, duty), QPoint(100, duty) };
}

// Seuil de démarrage : duty min de la table de calib où le ventilo tourne
// encore (rpm > 0). 0 si non calibré / toujours à l'arrêt / seuil non mesuré.
// On n'accepte le seuil que si un palier INFÉRIEUR l'a vu à l'arrêt (rpm == 0) :
// une table à 2 points issue d'une ancienne migration (max au 100 %, 0 au 0 %)
// donnerait sinon un faux seuil de 100 % -> ventilo bloqué éteint sous 100 %.
static int calibStartDuty(const QVector<QPoint>& table)
{
    int start = 0, lowestZeroDuty = 101;
    for (const QPoint& p : table) {
        if (p.y() > 0 && (start == 0 || p.x() < start)) start = p.x();
        if (p.y() == 0 && p.x() < lowestZeroDuty)       lowestZeroDuty = p.x();
    }
    // Seuil valable seulement si mesuré entre un palier tournant et un palier
    // arrêté juste en dessous (paliers rapprochés = vrai balayage, pas 100/0).
    if (start > 0 && lowestZeroDuty < start && (start - lowestZeroDuty) <= 15)
        return start;
    return 0;
}

// % réel d'un ventilo calibré : interpolation inverse RPM courant -> duty sur
// la table (duty, rpm) mesurée. La table est stockée duty décroissant ; on la
// parcourt par RPM croissant. Hors plage -> borné à [0, 100].
static int calibratedPct(const QVector<QPoint>& table, int rpm)
{
    if (table.size() < 2) return -1;
    // Bornes par RPM (indépendant de l'ordre de stockage)
    int loRpm = table.first().y(), loDuty = table.first().x();
    int hiRpm = loRpm, hiDuty = loDuty;
    for (const QPoint& p : table) {
        if (p.y() < loRpm) { loRpm = p.y(); loDuty = p.x(); }
        if (p.y() > hiRpm) { hiRpm = p.y(); hiDuty = p.x(); }
    }
    if (rpm <= loRpm) return std::clamp(loDuty, 0, 100);
    if (rpm >= hiRpm) return std::clamp(hiDuty, 0, 100);
    // Segment encadrant : trie implicite par recherche du couple le plus serré
    int bestLoR = loRpm, bestLoD = loDuty, bestHiR = hiRpm, bestHiD = hiDuty;
    for (const QPoint& a : table) {
        if (a.y() <= rpm && a.y() >= bestLoR) { bestLoR = a.y(); bestLoD = a.x(); }
        if (a.y() >= rpm && a.y() <= bestHiR) { bestHiR = a.y(); bestHiD = a.x(); }
    }
    if (bestHiR == bestLoR) return std::clamp(bestHiD, 0, 100);
    const double d = bestLoD + double(bestHiD - bestLoD)
                     * (rpm - bestLoR) / double(bestHiR - bestLoR);
    return std::clamp(int(std::lround(d)), 0, 100);
}

// ─────────────────────────────────────────────────────────────────────────────
PumpWidget::PumpWidget(QWidget* parent)
    : QWidget(parent)
{
    qRegisterMetaType<QVector<int>>("QVector<int>");
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<QStringList>("QStringList");
    qRegisterMetaType<FanChannelConfig>("NZXTKrakenPump::FanChannelConfig");
    qRegisterMetaType<QMap<QString, QVector<QPoint>>>("QMap<QString,QVector<QPoint>>");

    // Unité de température : suit la locale/langue OpenRGB (°F si région US),
    // même système que le plugin LCD.
    m_fahrenheit  = OpenRGBSettings::prefersFahrenheit();
    m_krakenGroup = QStringLiteral("NZXT Kraken");

    buildUI();
    loadConfig();
    applyPumpCfgToUi();
    setCalibBadge(!m_calibTables.isEmpty());

    // Thème initial dérivé de la palette OpenRGB
    setDarkTheme(palette().color(QPalette::Window).lightness() < 128);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { saveConfig(); });

    // ── Worker thread ───────────────────────────────────────────────────────
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("KrakenPumpWorker"));
    m_worker = new PumpWorker(&m_device);
    // Headers vides mémorisés : masqués dès l'énumération (avant le start,
    // appel direct sûr), la surveillance continue côté worker.
    m_worker->presetDeadChannels(m_deadIds);
    m_worker->presetPumpTachChannels(m_pumpTachIds);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &PumpWorker::start);

    connect(m_worker, &PumpWorker::deviceOpened, this,
            [this](const QString& name, bool ok, bool /*hasFan*/) {
        m_lblDeviceName->setText(ok ? name : QStringLiteral("No Kraken device detected"));
        m_krakenGroup = ok ? name : QStringLiteral("NZXT Kraken");
    });
    connect(m_worker, &PumpWorker::statusText, this, [this](const QString& t) {
        m_lblStatus->setText(t);
    });
    connect(m_worker, &PumpWorker::readings, this,
            [this](float temp, int pumpRpm, int pumpDuty, int fanRpm, int /*fanDuty*/) {
        // Même format et mêmes éléments que la ligne de statut du plugin LCD
        const bool fahr  = m_fahrenheit;
        const double liq = fahr ? temp * 9.0 / 5.0 + 32.0 : temp;
        m_lblStatus->setText(QStringLiteral("Liq. %1°%2  Pump %3 RPM  Fan %4 RPM")
                                 .arg(liq, 0, 'f', 1)
                                 .arg(fahr ? QStringLiteral("F") : QStringLiteral("C"))
                                 .arg(pumpRpm).arg(fanRpm));
        if (m_pumpPanel)
            m_pumpPanel->reading->setText(QStringLiteral("%1 RPM  %2%")
                                              .arg(pumpRpm).arg(pumpDuty));
    });

    // ── Signaux ventilateurs ────────────────────────────────────────────────
    connect(m_worker, &PumpWorker::fanChannelsReady, this,
            [this](const QStringList& ids, const QStringList& names,
                   const QStringList& groups) {
        rebuildFanPanels(ids, names, groups);
    });
    connect(m_worker, &PumpWorker::tempSourcesReady, this,
            [this](const QStringList& ids, const QStringList& names) {
        m_srcIds   = ids;
        m_srcNames = names;
        for (ChannelPanel* p : m_fanPanels)
            fillSourceCombo(p->source, fanCfg(p->id).sourceId);
    });
    connect(m_worker, &PumpWorker::fanReadings, this,
            [this](const QStringList& ids, const QVector<int>& rpms,
                   const QVector<int>& duties) {
        for (ChannelPanel* p : m_fanPanels) {
            if (m_identSpin.contains(p->id))
                continue;   // garde le texte "Identify" pendant les 5 s
            const int i = ids.indexOf(p->id);
            if (i < 0)
                continue;
            // Canal calibré : % = interp inverse RPM courant -> duty sur la
            // table mesurée (le duty commandé ne reflète pas le régime réel).
            int pct = duties.value(i);
            const auto tbl = m_calibTables.constFind(p->id);
            if (tbl != m_calibTables.constEnd()) {
                const int c = calibratedPct(tbl.value(), rpms.value(i));
                if (c >= 0) pct = c;
            }
            p->reading->setText(QStringLiteral("%1 RPM  %2%")
                                    .arg(rpms.value(i)).arg(pct));
        }
    });
    connect(m_worker, &PumpWorker::deadChannelsChanged, this,
            [this](const QStringList& ids) {
        m_deadIds = ids;
        if (m_saveTimer) m_saveTimer->start();
    });
    connect(m_worker, &PumpWorker::pumpTachChannelsChanged, this,
            [this](const QStringList& ids) {
        m_pumpTachIds = ids;
        if (m_saveTimer) m_saveTimer->start();
    });
    connect(m_worker, &PumpWorker::calibrationProgress, this, [this](int pct) {
        if (m_btnCalib && !m_btnCalib->isEnabled())
            m_btnCalib->setText(QStringLiteral("Calibrating… %1%").arg(pct));
    });
    connect(m_worker, &PumpWorker::calibrationFinished, this,
            [this](const QMap<QString, QVector<QPoint>>& tables) {
        for (auto it = tables.constBegin(); it != tables.constEnd(); ++it)
            m_calibTables.insert(it.key(), it.value());
        if (m_btnCalib) {
            m_btnCalib->setText(QStringLiteral("Calibrate fans"));
            m_btnCalib->setEnabled(true);
            setCalibBadge(!m_calibTables.isEmpty());
        }
        // Propager les seuils de démarrage mesurés au worker (plancher/kick)
        for (ChannelPanel* p : m_fanPanels)
            if (!p->isPump)
                pushFanCfg(p);
        if (m_saveTimer) m_saveTimer->start();
    });
    connect(m_worker, &PumpWorker::tempValues, this,
            [this](const QStringList& ids, const QVector<float>& values) {
        m_srcVals = values;
        for (ChannelPanel* p : m_fanPanels) {
            const int i = ids.indexOf(fanCfg(p->id).sourceId);
            if (i >= 0)
                p->editor->setLiveTemp(values.value(i));
        }
    });

    m_thread->start();

    m_ready = true;

    // Pousser la config pompe chargée vers le worker
    dispatchPump();
}

PumpWidget::~PumpWidget()
{
    m_ready = false;
    if (m_saveTimer) { m_saveTimer->stop(); saveConfig(); }

    if (m_worker) {
        // stop() rend les canaux LHM au BIOS (failsafe) puis ferme le device
        QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait(3000);
        delete m_worker;
        m_worker = nullptr;
    }
}

// ─── UI ───────────────────────────────────────────────────────────────────────
void PumpWidget::buildUI()
{
    // Mêmes marges/espacements que le plugin LCD (root 16,12,16,12 / col 8)
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    // Header : nom device + statut empilés (même structure que le plugin LCD),
    // bouton de calibration des ventilateurs à droite.
    m_lblDeviceName = new QLabel(QStringLiteral("Detecting…"));
    m_lblDeviceName->setStyleSheet("font-weight:600;font-size:13px;");
    m_lblStatus = new QLabel(QStringLiteral("Starting…"));
    m_lblStatus->setStyleSheet(statusLabelStyle(m_darkTheme));

    m_btnCalib = new QPushButton(QStringLiteral("Calibrate fans"));
    m_btnCalib->setFixedHeight(22);
    m_btnCalib->setCursor(Qt::ArrowCursor);
    m_btnCalib->setToolTip(QStringLiteral("Show each fan's real speed"));
    m_calibBadge = new CornerBadge(this);   // overlay, chevauche le coin du bouton
    m_calibBadge->hide();
    connect(m_btnCalib, &QPushButton::clicked, this, [this] {
        if (!m_worker) return;
        // Nouvelle passe : effacer les tables précédentes et retirer le badge
        // jusqu'à la fin (le % réaffiche le duty commandé en attendant).
        m_calibTables.clear();
        setCalibBadge(false);
        m_btnCalib->setEnabled(false);
        m_btnCalib->setText(QStringLiteral("Calibrating…"));
        if (m_saveTimer) m_saveTimer->start();
        QMetaObject::invokeMethod(m_worker, "calibrate", Qt::QueuedConnection);
    });

    auto* headRow = new QHBoxLayout();
    headRow->setSpacing(8);
    auto* headCol = new QVBoxLayout();
    headCol->setSpacing(2);
    headCol->addWidget(m_lblDeviceName);
    headCol->addWidget(m_lblStatus);
    headRow->addLayout(headCol, 1);
    headRow->addWidget(m_btnCalib, 0, Qt::AlignTop);
    root->addLayout(headRow);

    // Liste scrollable des blocs hardware
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Fond transparent : sans ça le viewport peint QPalette::Base (plus foncé
    // que la fenêtre OpenRGB) derrière les blocs. Stylesheet explicite requis,
    // OpenRGB stylant les QAbstractScrollArea via son QSS applicatif.
    scroll->setObjectName(QStringLiteral("pumpScroll"));
    scroll->setStyleSheet(QStringLiteral(
        "#pumpScroll{background:transparent;}"
        "#pumpScroll > QWidget#qt_scrollarea_viewport{background:transparent;}"));
    scroll->viewport()->setAutoFillBackground(false);
    auto* container = new QWidget();
    container->setObjectName(QStringLiteral("pumpScrollBody"));
    container->setStyleSheet(QStringLiteral("#pumpScrollBody{background:transparent;}"));
    container->setAutoFillBackground(false);
    m_listLay = new QVBoxLayout(container);
    // Aucune marge : la scrollbar (hors viewport) n'a pas besoin de réserve,
    // et un 6 px à droite seul décentrait les blocs.
    m_listLay->setContentsMargins(0, 0, 0, 0);
    m_listLay->setSpacing(10);
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    m_listLay->addStretch();

    // Bloc Kraken initial (pompe seule, avant la découverte des canaux)
    rebuildFanPanels({}, {}, {});
}

QFrame* PumpWidget::makeSeparator()
{
    auto* line = new QFrame();
    line->setFixedHeight(1);
    line->setStyleSheet(separatorStyle(m_darkTheme));
    m_separators.append(line);
    return line;
}

PumpWidget::ChannelPanel* PumpWidget::createPanel(const QString& id,
                                                  const QString& title, bool isPump)
{
    auto* p = new ChannelPanel;
    p->id          = id;
    p->defaultName = title;
    p->isPump      = isPump;
    p->iconType    = isPump ? IconPump
                   : id.startsWith(QStringLiteral("/gpu")) ? IconGpu
                   : IconFan;

    p->box = new QWidget();
    p->box->setCursor(Qt::PointingHandCursor);
    p->box->installEventFilter(this);
    m_boxOwner.insert(p->box, p);

    auto* lay = new QVBoxLayout(p->box);
    lay->setContentsMargins(2, 4, 2, 4);
    lay->setSpacing(6);

    // ── Rangée d'en-tête : icône + nom + RPM/duty + sonde + mode ────────────
    auto* head = new QHBoxLayout();
    head->setSpacing(8);

    p->icon = new QLabel();
    p->icon->setPixmap(makeChannelIcon(p->iconType, m_darkTheme));
    p->icon->setFixedSize(28, 28);
    if (!isPump) {
        // Clic sur l'icône = Identify (100 % pendant 5 s, pales animées)
        p->icon->setToolTip(QStringLiteral("Identify"));
        p->icon->installEventFilter(this);
        m_iconOwner.insert(p->icon, p);
    }
    head->addWidget(p->icon);

    p->name = new QLineEdit(title);
    p->name->setReadOnly(true);
    p->name->setFrame(false);
    p->name->setStyleSheet(nameEditStyle());
    QFont nameFont = p->name->font();
    nameFont.setPixelSize(13);
    nameFont.setWeight(QFont::DemiBold);
    p->name->setFont(nameFont);
    p->name->setCursor(Qt::IBeamCursor);
    p->name->setToolTip(QStringLiteral("Rename"));
    p->name->installEventFilter(this);
    m_nameOwner.insert(p->name, p);
    fitNameWidth(p->name);
    connect(p->name, &QLineEdit::textChanged, this,
            [p] { fitNameWidth(p->name); });
    head->addWidget(p->name);
    head->addStretch(1);   // le reste de la rangée = zone de clic repli/dépli

    p->reading = new QLabel(QStringLiteral("—"));
    p->reading->setStyleSheet(statusLabelStyle(m_darkTheme));
    p->reading->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    head->addWidget(p->reading);

    // Sonde de température (dans l'en-tête, visible rangée repliée)
    p->source = new QComboBox();
    p->source->setMinimumWidth(120);
    head->addWidget(p->source);

    p->mode = new QComboBox();
    p->mode->addItems({ QStringLiteral("Silent"),
                        QStringLiteral("Performance"),
                        QStringLiteral("Fixed"),
                        QStringLiteral("Custom") });
    p->mode->setCurrentIndex(int(FanSilent));
    p->mode->setMinimumWidth(140);
    head->addWidget(p->mode);
    lay->addLayout(head);

    // ── Corps repliable : rangée de contrôles + courbe (replié par défaut) ──
    p->body = new QWidget();
    auto* bodyLay = new QVBoxLayout(p->body);
    bodyLay->setContentsMargins(0, 0, 0, 0);

    // Graphe + colonne Step up/down à sa droite (zone vide de l'éditeur),
    // puis Reset/Apply sous le graphe. (Identify : clic sur l'icône.)
    {
        auto* mid = new QHBoxLayout();
        mid->setSpacing(6);

        p->editor = new PumpCurveEditor();
        p->editor->setMinDuty(isPump ? 20 : 0);
        p->editor->setFahrenheit(m_fahrenheit);
        p->editor->setRightMargin(16);   // la colonne de droite prend le relais
        mid->addWidget(p->editor, 1);

        // Colonne de contrôles à droite, bornée sur la hauteur de la zone
        // tracée (plotRect : 10 px en haut, 38 px d'étiquettes en bas) :
        // Step up / Step down en haut, Reset / Apply en bas, largeur unique.
        constexpr int SIDE_W = 84;
        auto* side = new QVBoxLayout();
        side->setSpacing(2);
        side->addSpacing(10);

        auto makeStep = [&](const QString& label, int def) -> QSpinBox* {
            auto* lbl = new QLabel(label);
            lbl->setStyleSheet(statusLabelStyle(m_darkTheme));
            side->addWidget(lbl);
            auto* sp = new QSpinBox();
            sp->setRange(1, 100);
            sp->setValue(def);
            sp->setSuffix(QStringLiteral(" %/s"));
            sp->setFixedSize(SIDE_W, 22);
            sp->setCursor(Qt::ArrowCursor);
            side->addWidget(sp);
            return sp;
        };
        p->stepUp = makeStep(QStringLiteral("Step up"), 5);
        side->addSpacing(8);
        p->stepDown = makeStep(QStringLiteral("Step down"), 5);

        side->addStretch();

        auto* reset = new QPushButton(QStringLiteral("Reset"));
        reset->setFixedSize(SIDE_W, 22);
        reset->setCursor(Qt::ArrowCursor);
        connect(reset, &QPushButton::clicked, this, [this, p] { resetPanel(p); });
        side->addWidget(reset);
        side->addSpacing(4);

        auto* apply = new QPushButton(QStringLiteral("Apply"));
        apply->setFixedSize(SIDE_W, 22);
        apply->setCursor(Qt::ArrowCursor);
        connect(apply, &QPushButton::clicked, this, [this, p, apply] {
            showApplyPopover(p, apply);
        });
        side->addWidget(apply);
        side->addSpacing(38);

        mid->addLayout(side);
        bodyLay->addLayout(mid);

        connect(p->stepUp, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, p](int v) {
            fanCfg(p->id).riseRate = v;
            if (p->isPump) { if (m_ready) dispatchPump(); }
            else           pushFanCfg(p);
        });
        connect(p->stepDown, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, p](int v) {
            fanCfg(p->id).fallRate = v;
            if (p->isPump) { if (m_ready) dispatchPump(); }
            else           pushFanCfg(p);
        });
    }

    lay->addWidget(p->body);
    p->body->setVisible(false);   // replié par défaut

    // ── Connexions ──────────────────────────────────────────────────────────
    if (isPump) {
        connect(p->mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { onPumpModeChanged(); });
        connect(p->source, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, p](int idx) {
            fanCfg(p->id).sourceId = p->source->itemData(idx).toString();
            if (m_ready) dispatchPump();
        });
        connect(p->editor, &PumpCurveEditor::curveChanged,
                this, [this] { onPumpCurveEdited(); });
    } else {
        connect(p->mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, p](int) { onFanModeChanged(p); });
        connect(p->source, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, p](int idx) {
            fanCfg(p->id).sourceId = p->source->itemData(idx).toString();
            pushFanCfg(p);
        });
        connect(p->editor, &PumpCurveEditor::curveChanged,
                this, [this, p] { onFanCurveEdited(p); });
    }

    // Validation du renommage (Entrée ou perte de focus)
    connect(p->name, &QLineEdit::editingFinished, this, [this, p] {
        p->name->setReadOnly(true);
        p->name->deselect();
        QString n = p->name->text().trimmed();
        if (n.isEmpty()) {
            n = p->defaultName;
            p->name->setText(n);
        }
        fanCfg(p->id).name = (n == p->defaultName) ? QString() : n;
        if (m_ready)
            m_saveTimer->start();
    });
    return p;
}

void PumpWidget::setPanelExpanded(ChannelPanel* p, bool expanded)
{
    p->body->setVisible(expanded);
}

// Clic n'importe où sur la rangée -> repli/dépli ;
// simple clic sur le nom -> renommage inline (curseur texte au survol).
bool PumpWidget::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonRelease) {
        auto itI = m_iconOwner.find(obj);
        if (itI != m_iconOwner.end()) {
            startIdentify(itI.value());
            return true;   // ne pas replier/déplier la rangée
        }
        auto itB = m_boxOwner.find(obj);
        if (itB != m_boxOwner.end()) {
            setPanelExpanded(itB.value(), !itB.value()->body->isVisible());
            return false;
        }
        auto itN = m_nameOwner.find(obj);
        if (itN != m_nameOwner.end() && itN.value()->name->isReadOnly()) {
            QLineEdit* le = itN.value()->name;
            le->setReadOnly(false);
            le->setFocus(Qt::MouseFocusReason);
            le->selectAll();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void PumpWidget::fillSourceCombo(QComboBox* combo, const QString& selectedId)
{
    if (!combo) return;
    QSignalBlocker block(combo);
    combo->clear();
    for (int i = 0; i < m_srcIds.size(); ++i)
        combo->addItem(m_srcNames.value(i), m_srcIds.value(i));
    const int idx = combo->findData(selectedId);
    if (idx >= 0) combo->setCurrentIndex(idx);
}

void PumpWidget::rebuildFanPanels(const QStringList& ids, const QStringList& names,
                                  const QStringList& groups)
{
    m_chIds = ids; m_chNames = names; m_chGroups = groups;

    // Tout supprimer (les panneaux sont enfants des blocs)
    m_nameOwner.clear();
    m_boxOwner.clear();
    m_iconOwner.clear();
    m_identSpin.clear();
    if (m_spinTimer) m_spinTimer->stop();
    for (QFrame* f : m_groupFrames) {
        m_listLay->removeWidget(f);
        f->deleteLater();
    }
    m_groupFrames.clear();
    m_separators.clear();
    for (ChannelPanel* p : m_fanPanels)
        delete p;   // les widgets meurent avec leur bloc
    m_fanPanels.clear();
    m_pumpPanel = nullptr;

    // Ordre des sections : Kraken d'abord (pompe [+ fan]), puis les groupes
    // LHM dans leur ordre d'apparition.
    struct Section { QString title; QVector<int> idx; };   // idx dans ids (-1 = pompe)
    QVector<Section> sections;
    sections.append({ m_krakenGroup, { -1 } });
    for (int i = 0; i < ids.size(); ++i) {
        const QString g = (ids.value(i) == KRAKEN_FAN_ID) ? m_krakenGroup
                                                          : groups.value(i);
        int s = -1;
        for (int k = 0; k < sections.size(); ++k)
            if (sections[k].title == g) { s = k; break; }
        if (s < 0) { sections.append({ g, {} }); s = sections.size() - 1; }
        sections[s].idx.append(i);
    }

    // Un bloc (cadre) par section, inséré avant le stretch final
    int at = m_listLay->count() - 1;
    for (const Section& sec : sections) {
        auto* frame = new QFrame();
        frame->setObjectName(QStringLiteral("groupBlock"));
        frame->setStyleSheet(groupFrameStyle(m_darkTheme));
        auto* v = new QVBoxLayout(frame);
        v->setContentsMargins(10, 8, 10, 8);
        v->setSpacing(4);

        auto* titleLbl = new QLabel(sec.title);
        titleLbl->setStyleSheet("font-weight:600;font-size:13px;border:none;background:transparent;");
        v->addWidget(titleLbl);

        bool first = true;
        for (int i : sec.idx) {
            if (!first)
                v->addWidget(makeSeparator());
            first = false;

            if (i < 0) {   // pompe
                ChannelPanel* p = createPanel(KRAKEN_PUMP_ID, QStringLiteral("Pump"), true);
                m_pumpPanel = p;
                applyPumpCfgToUi();
                v->addWidget(p->box);
                m_fanPanels.append(p);
                continue;
            }

            const QString id = ids.value(i);
            const FanChannelConfig& cfg0 = fanCfg(id);
            const QString shown = cfg0.name.isEmpty() ? names.value(i) : cfg0.name;

            ChannelPanel* p = createPanel(id, shown, false);
            p->defaultName = names.value(i);
            {
                QSignalBlocker b(p->mode);
                p->mode->setCurrentIndex(std::clamp(cfg0.mode, 0, 3));
            }
            fillSourceCombo(p->source, cfg0.sourceId);
            {
                QSignalBlocker bu(p->stepUp),  bd(p->stepDown);
                p->stepUp->setValue(std::clamp(cfg0.riseRate, 1, 100));
                p->stepDown->setValue(std::clamp(cfg0.fallRate, 1, 100));
            }
            refreshPanelEditor(p);
            v->addWidget(p->box);
            m_fanPanels.append(p);

            // Pousser la config persistée vers le worker (plus de mode Auto :
            // tous les canaux sont pilotés)
            pushFanCfg(p);
        }

        m_listLay->insertWidget(at++, frame);
        m_groupFrames.append(frame);
    }
}

// ─── Pompe ────────────────────────────────────────────────────────────────────
void PumpWidget::applyPumpCfgToUi()
{
    if (!m_pumpPanel) return;
    const FanChannelConfig& cfg = fanCfg(KRAKEN_PUMP_ID);
    {
        QSignalBlocker b(m_pumpPanel->mode);
        m_pumpPanel->mode->setCurrentIndex(std::clamp(cfg.mode, 0, 3));
    }
    if (!cfg.name.isEmpty())
        m_pumpPanel->name->setText(cfg.name);
    fillSourceCombo(m_pumpPanel->source, cfg.sourceId);
    {
        QSignalBlocker bu(m_pumpPanel->stepUp), bd(m_pumpPanel->stepDown);
        m_pumpPanel->stepUp->setValue(std::clamp(cfg.riseRate, 1, 100));
        m_pumpPanel->stepDown->setValue(std::clamp(cfg.fallRate, 1, 100));
    }
    m_pumpPanel->editor->setFixedMode(cfg.mode == FanFixed);
    m_pumpPanel->editor->setPoints(effectivePumpPoints(cfg));
}

QVector<QPoint> PumpWidget::effectivePumpPoints(const FanChannelConfig& cfg) const
{
    switch (cfg.mode) {
        case FanSilent:
        case FanPerformance:
            return pumpPresetPoints(cfg.mode);
        case FanFixed:
            return fixedPoints(std::clamp(cfg.fixedDuty, 20, 100));
        default:   // Custom
            return cfg.points.isEmpty() ? pumpPresetPoints(FanSilent)
                                        : cfg.points;
    }
}

void PumpWidget::onPumpModeChanged()
{
    FanChannelConfig& cfg = fanCfg(KRAKEN_PUMP_ID);
    cfg.mode = m_pumpPanel->mode->currentIndex();
    m_pumpPanel->editor->setFixedMode(cfg.mode == FanFixed);
    m_pumpPanel->editor->setPoints(effectivePumpPoints(cfg));
    if (m_ready) dispatchPump();
}

void PumpWidget::onPumpCurveEdited()
{
    FanChannelConfig& cfg = fanCfg(KRAKEN_PUMP_ID);
    const QVector<QPoint> pts = m_pumpPanel->editor->points();

    // Mode Fixed : drag des 2 points liés = nouveau duty fixe, on RESTE en
    // Fixed ; un point ajouté fait basculer en Custom.
    if (cfg.mode == FanFixed && pts.size() == 2
            && pts[0].y() == pts[1].y()) {
        cfg.fixedDuty = pts[0].y();
        if (m_ready) dispatchPump();
        return;
    }

    if (cfg.mode != FanCustom) {
        cfg.mode = FanCustom;
        QSignalBlocker block(m_pumpPanel->mode);
        m_pumpPanel->mode->setCurrentIndex(FanCustom);
        m_pumpPanel->editor->setFixedMode(false);
    }
    cfg.points = pts;
    if (m_ready) dispatchPump();
}

// Pompe : toujours la boucle logicielle du worker (courbe plate 1 Hz, plancher
// 20 % + ré-écriture sur mismatch côté worker) — la sémantique de la courbe
// native n'est pas fiable sur Elite/Elite V2 (nouveau protocole 0x72).
void PumpWidget::dispatchPump()
{
    if (!m_worker || !m_pumpPanel) return;
    const FanChannelConfig& cfg = fanCfg(KRAKEN_PUMP_ID);

    FanChannelConfig eff = cfg;
    eff.points = effectivePumpPoints(cfg);   // le worker interpole sur l'effectif
    QMetaObject::invokeMethod(m_worker, "setFanConfig", Qt::QueuedConnection,
                              Q_ARG(QString, KRAKEN_PUMP_ID),
                              Q_ARG(NZXTKrakenPump::FanChannelConfig, eff));
    m_saveTimer->start();
}

// ─── Ventilateurs ─────────────────────────────────────────────────────────────
FanChannelConfig& PumpWidget::fanCfg(const QString& id)
{
    if (!m_fanCfgs.contains(id)) {
        FanChannelConfig def;
        if (id == KRAKEN_PUMP_ID) {
            def.mode     = FanSilent;
            def.sourceId = QStringLiteral("liquid");
            def.points   = pumpPresetPoints(FanSilent);
        } else if (id == KRAKEN_FAN_ID) {
            def.mode     = FanSilent;
            def.sourceId = QStringLiteral("liquid");
            def.points   = fanPresetPoints(FanSilent);
        } else {
            def.mode     = FanSilent;
            def.points   = fanPresetPoints(FanSilent);
            def.sourceId = m_srcIds.value(1, QStringLiteral("liquid"));
            for (const QString& s : m_srcIds)
                if (s.startsWith(QStringLiteral("/amdcpu"))
                    || s.startsWith(QStringLiteral("/intelcpu"))) { def.sourceId = s; break; }
        }
        m_fanCfgs.insert(id, def);
    }
    return m_fanCfgs[id];
}

QVector<QPoint> PumpWidget::effectiveFanPoints(const FanChannelConfig& cfg) const
{
    switch (cfg.mode) {
        case FanSilent:
        case FanPerformance:
            return fanPresetPoints(cfg.mode);
        case FanFixed:
            return fixedPoints(std::clamp(cfg.fixedDuty, 0, 100));
        default:   // Custom
            return cfg.points.isEmpty() ? fanPresetPoints(FanSilent)
                                        : cfg.points;
    }
}

void PumpWidget::refreshPanelEditor(ChannelPanel* p)
{
    const FanChannelConfig& cfg = fanCfg(p->id);
    p->editor->setFixedMode(cfg.mode == FanFixed);
    p->editor->setPoints(effectiveFanPoints(cfg));
}

void PumpWidget::pushFanCfg(ChannelPanel* p)
{
    if (!m_worker) return;
    const FanChannelConfig& stored = fanCfg(p->id);
    FanChannelConfig eff = stored;
    eff.points = effectiveFanPoints(stored);   // le worker interpole sur l'effectif
    // Seuil de démarrage mesuré (calib) : plancher zero-RPM + kick par ventilo
    const auto tbl = m_calibTables.constFind(p->id);
    if (tbl != m_calibTables.constEnd())
        eff.startDuty = calibStartDuty(tbl.value());
    QMetaObject::invokeMethod(m_worker, "setFanConfig", Qt::QueuedConnection,
                              Q_ARG(QString, p->id),
                              Q_ARG(NZXTKrakenPump::FanChannelConfig, eff));
    if (m_ready)
        m_saveTimer->start();
}

void PumpWidget::onFanModeChanged(ChannelPanel* p)
{
    fanCfg(p->id).mode = p->mode->currentIndex();
    refreshPanelEditor(p);
    pushFanCfg(p);
}

void PumpWidget::onFanCurveEdited(ChannelPanel* p)
{
    FanChannelConfig& cfg = fanCfg(p->id);
    const QVector<QPoint> pts = p->editor->points();

    // Mode Fixed : drag des 2 points liés = nouveau duty fixe, on RESTE en
    // Fixed ; un point ajouté fait basculer en Custom.
    if (cfg.mode == FanFixed && pts.size() == 2
            && pts[0].y() == pts[1].y()) {
        cfg.fixedDuty = pts[0].y();
        pushFanCfg(p);
        return;
    }

    if (cfg.mode != FanCustom) {
        cfg.mode = FanCustom;
        QSignalBlocker block(p->mode);
        p->mode->setCurrentIndex(FanCustom);
        p->editor->setFixedMode(false);
    }
    cfg.points = pts;
    pushFanCfg(p);
}

// Recharge l'UI d'un panneau depuis sa config (signaux bloqués), puis pousse.
void PumpWidget::applyPanelCfgToUi(ChannelPanel* p)
{
    const FanChannelConfig& cfg = fanCfg(p->id);
    {
        QSignalBlocker b(p->mode);
        p->mode->setCurrentIndex(std::clamp(cfg.mode, 0, 3));
    }
    {
        QSignalBlocker bu(p->stepUp), bd(p->stepDown);
        p->stepUp->setValue(std::clamp(cfg.riseRate, 1, 100));
        p->stepDown->setValue(std::clamp(cfg.fallRate, 1, 100));
    }
    if (p->isPump) {
        p->editor->setFixedMode(cfg.mode == FanFixed);
        p->editor->setPoints(effectivePumpPoints(cfg));
        if (m_ready) dispatchPump();
    } else {
        refreshPanelEditor(p);
        pushFanCfg(p);
    }
}

// Identify (clic sur l'icône) : le worker force 100 % pendant 5 s ; côté UI,
// les pales de l'icône tournent — accélération progressive, palier à pleine
// vitesse, puis décélération progressive — et la lecture affiche "Identify".
void PumpWidget::startIdentify(ChannelPanel* p)
{
    if (p->isPump || m_identSpin.contains(p->id))
        return;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "identify", Qt::QueuedConnection,
                                  Q_ARG(QString, p->id));
    if (!m_uiClock.isValid()) m_uiClock.start();
    m_identSpin.insert(p->id, { m_uiClock.elapsed(), 0.0 });
    p->reading->setText(QStringLiteral("Identify"));

    if (!m_spinTimer) {
        m_spinTimer = new QTimer(this);
        m_spinTimer->setInterval(30);
        connect(m_spinTimer, &QTimer::timeout, this, [this] {
            const qint64 now = m_uiClock.elapsed();
            const qreal  dt  = m_spinTimer->interval() / 1000.0;
            for (auto it = m_identSpin.begin(); it != m_identSpin.end(); ) {
                const qint64 t = now - it.value().startMs;
                ChannelPanel* panel = nullptr;
                for (ChannelPanel* c : m_fanPanels)
                    if (c->id == it.key()) { panel = c; break; }

                if (t >= 8500 || !panel) {
                    // L'angle final est un multiple de 360/7 (symétrie des
                    // pales) : l'icône de repos est identique, pas de saut.
                    if (panel)
                        panel->icon->setPixmap(
                            makeChannelIcon(panel->iconType, m_darkTheme));
                    it = m_identSpin.erase(it);
                    continue;
                }

                // Profil (découplé du device, qui reste 5 s à 100 %) :
                // accélération 2 s -> pleine vitesse 4,5 s -> décélération 2 s.
                if (t <= 6500) {
                    const qreal f = (t < 2000)
                        ? (t / 2000.0) * (t / 2000.0)
                        : 1.0;
                    it.value().angle += 1000.0 * f * dt;
                } else {
                    // Décélération analytique (ease-out cubique) : atterrit
                    // EXACTEMENT sur un multiple de 360/7, pente initiale
                    // raccordée à la vitesse du palier (1000 °/s).
                    IdentSpin& s = it.value();
                    if (s.decelStart < 0) {
                        s.decelStart = s.angle;
                        const qreal step    = 360.0 / 7.0;
                        const qreal natural = s.angle + 1000.0 * 2.0 / 3.0;
                        s.decelTarget = std::ceil(natural / step) * step;
                    }
                    const qreal u    = (8500.0 - t) / 2000.0;   // 1 -> 0
                    const qreal span = s.decelTarget - s.decelStart;
                    s.angle = s.decelTarget - span * u * u * u;
                }

                panel->icon->setPixmap(makeChannelIcon(panel->iconType, m_darkTheme,
                                                       it.value().angle));
                ++it;
            }
            if (m_identSpin.isEmpty())
                m_spinTimer->stop();
        });
    }
    m_spinTimer->start();
}

// Reset : mode Silent, courbe = preset Silent, duty fixe et rampes par
// défaut. Nom et sonde conservés.
void PumpWidget::resetPanel(ChannelPanel* p)
{
    FanChannelConfig& cfg = fanCfg(p->id);
    cfg.mode      = p->isPump ? int(FanSilent) : int(FanSilent);
    cfg.points    = p->isPump ? pumpPresetPoints(FanSilent)
                              : fanPresetPoints(FanSilent);
    cfg.fixedDuty = 50;
    cfg.riseRate  = 5;
    cfg.fallRate  = 5;
    applyPanelCfgToUi(p);
    m_saveTimer->start();
}

// Popover "Apply" — mêmes traits que les popovers couleur du plugin LCD :
// QFrame Qt::Popup translucide, fond arrondi peint (10 px), palette dédiée,
// boutons "surface". Liste scrollable des autres ventilateurs à cocher ; la
// courbe (mode, points, rampes) du canal source est copiée sur les cochés.
namespace {
class ApplyPopover : public QFrame {
public:
    explicit ApplyPopover(bool dark, QWidget* parent)
        : QFrame(parent), m_dark(dark)
    {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        const QString text    = dark ? "#FFFFFF" : "#1A1A1E";
        const QString surface = dark ? "#1F1F23" : "#EBEBEE";
        const QString border  = dark ? "#2A2A2E" : "#D0D0D4";
        const QString hover   = dark ? "#22222A" : "#DDDDE0";
        const QString accent  = "#2ECC71";   // même vert que la pastille Calibrate
        // La case à cocher reprend le fond + le contour des boutons Apply/Cancel
        // (même surface, même bordure -> cohérence, visible en clair comme en
        // sombre). Cochée : remplie en accent vert + coche blanche (PNG généré).
        const QString check = checkMarkImagePath();
        setStyleSheet(QString(
            "QLabel{color:%1;font-size:11px;background:transparent;border:none;}"
            "QCheckBox{color:%1;font-size:11px;background:transparent;spacing:7px;}"
            "QCheckBox::indicator{width:15px;height:15px;background:%2;"
            "border:1px solid %3;border-radius:4px;}"
            "QCheckBox::indicator:hover{background:%4;}"
            "QCheckBox::indicator:checked{background:%5;border:1px solid %5;"
            "image:url(\"%6\");}"
            "QPushButton{background:%2;color:%1;border:1px solid %3;"
            "border-radius:5px;padding:5px 10px;font-size:11px;}"
            "QPushButton:hover{background:%4;}"
            "QScrollArea{background:transparent;border:none;}")
            .arg(text, surface, border, hover, accent, check));
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(m_dark ? "#111113" : "#F0F0F2"));
        p.setPen(QPen(QColor(m_dark ? "#2A2A2E" : "#D0D0D4"), 1));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) { close(); return; }
        QFrame::keyPressEvent(e);
    }
private:
    bool m_dark;
};
} // anonymous namespace

void PumpWidget::showApplyPopover(ChannelPanel* src, QPushButton* anchor)
{
    // Cibles : tous les ventilateurs sauf la source et sauf la pompe
    QVector<ChannelPanel*> targets;
    for (ChannelPanel* t : m_fanPanels)
        if (t != src && !t->isPump)
            targets.append(t);

    auto* pop = new ApplyPopover(m_darkTheme, this);

    auto* lay = new QVBoxLayout(pop);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    // Aucune cible (pas de lhwm-wrapper.dll, ou seul canal) : état vide
    // explicite plutôt qu'un clic sans effet.
    if (targets.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral("No other fan channel to apply to."));
        lay->addWidget(empty);
        auto* close = new QPushButton(QStringLiteral("Close"));
        connect(close, &QPushButton::clicked, pop, &QWidget::close);
        lay->addWidget(close);
        pop->adjustSize();
        pop->move(anchor->mapToGlobal(QPoint(anchor->width() / 2 - pop->width() / 2,
                                             anchor->height() + 6)));
        pop->show();
        pop->setFocus();
        return;
    }

    auto* title = new QLabel(QStringLiteral("Apply curve to"));
    title->setStyleSheet(QStringLiteral(
        "font-weight:600;font-size:12px;background:transparent;border:none;"));
    lay->addWidget(title);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* listBody = new QWidget();
    listBody->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* listLay = new QVBoxLayout(listBody);
    listLay->setContentsMargins(0, 0, 4, 0);
    listLay->setSpacing(4);

    QVector<QCheckBox*> checks;
    for (ChannelPanel* t : targets) {
        auto* ck = new QCheckBox(t->name->text());
        ck->setProperty("chId", t->id);
        listLay->addWidget(ck);
        checks.append(ck);
    }
    listLay->addStretch();
    scroll->setWidget(listBody);
    scroll->setFixedHeight(std::min(160, targets.size() * 26 + 8));
    lay->addWidget(scroll);

    // Rangée de boutons en bas, comme le popover couleur du LCD
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* ok     = new QPushButton(QStringLiteral("Apply"));
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    btnRow->addWidget(ok, 1);
    btnRow->addWidget(cancel, 1);
    lay->addLayout(btnRow);

    connect(cancel, &QPushButton::clicked, pop, &QWidget::close);
    connect(ok, &QPushButton::clicked, this, [this, pop, src, checks] {
        const FanChannelConfig& sc = fanCfg(src->id);
        // Modes pompe et ventilos partagent les mêmes index (config v3) :
        // copie directe de mode, points, duty fixe et rampes.
        for (QCheckBox* ck : checks) {
            if (!ck->isChecked()) continue;
            const QString id = ck->property("chId").toString();
            FanChannelConfig& dst = fanCfg(id);
            dst.mode      = sc.mode;
            dst.points    = sc.points;
            dst.fixedDuty = sc.fixedDuty;
            dst.riseRate  = sc.riseRate;
            dst.fallRate  = sc.fallRate;
            for (ChannelPanel* t : m_fanPanels)
                if (t->id == id) { applyPanelCfgToUi(t); break; }
        }
        m_saveTimer->start();
        pop->close();
    });

    // Ancrage sous le bouton, centré, borné à l'écran (même logique que le
    // popover du plugin LCD).
    pop->adjustSize();
    const QPoint global = anchor->mapToGlobal(QPoint(0, anchor->height()));
    int x = global.x() + anchor->width() / 2 - pop->width() / 2;
    int y = global.y() + 6;
    QScreen* scr = QGuiApplication::screenAt(global);
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
    x = std::clamp(x, avail.left() + 6, avail.right() - pop->width() - 6);
    if (y + pop->height() > avail.bottom() - 6)
        y = anchor->mapToGlobal(QPoint(0, 0)).y() - pop->height() - 6;
    pop->move(x, y);
    pop->show();
    pop->setFocus();
}

// ─── Pastille de calibration (overlay) ─────────────────────────────────────────
void PumpWidget::setCalibBadge(bool on)
{
    if (!m_calibBadge) return;
    m_calibBadge->setVisible(on);
    if (on) positionCalibBadge();
}

void PumpWidget::positionCalibBadge()
{
    if (!m_calibBadge || !m_btnCalib || !m_calibBadge->isVisible())
        return;
    // Centre la pastille sur le coin haut-droit du bouton (chevauche le
    // contour, moitié dehors), en coordonnées du widget principal.
    const QPoint corner = m_btnCalib->mapTo(this, QPoint(m_btnCalib->width(), 0));
    constexpr int inset = 3;   // rentré vers l'intérieur (bas-gauche)
    m_calibBadge->move(corner.x() - m_calibBadge->width() / 2 - inset,
                       corner.y() - m_calibBadge->height() / 2 + inset);
    m_calibBadge->raise();
}

void PumpWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    positionCalibBadge();
}

void PumpWidget::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // Le layout n'est fixé qu'après le premier show : replacer au tick suivant.
    QTimer::singleShot(0, this, [this] { positionCalibBadge(); });
}

// ─── Thème / langue ───────────────────────────────────────────────────────────
void PumpWidget::setDarkTheme(bool dark)
{
    m_darkTheme = dark;
    const QString labStyle = statusLabelStyle(dark);
    if (m_lblStatus) m_lblStatus->setStyleSheet(labStyle);
    for (QFrame* f : m_groupFrames)
        f->setStyleSheet(groupFrameStyle(dark));
    for (QFrame* s : m_separators)
        s->setStyleSheet(separatorStyle(dark));
    for (ChannelPanel* p : m_fanPanels) {
        p->reading->setStyleSheet(labStyle);
        p->icon->setPixmap(makeChannelIcon(p->iconType, dark));
        p->editor->update();
    }
}

void PumpWidget::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    // OpenRGB applique sa palette (sombre/claire) à toute l'application ; on
    // suit ses changements de thème à chaud (même mécanisme que le plugin LCD).
    if (e && (e->type() == QEvent::PaletteChange ||
              e->type() == QEvent::ApplicationPaletteChange)) {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        if (dark != m_darkTheme)
            setDarkTheme(dark);
    }
    // OpenRGB change la langue à chaud (installTranslator -> LanguageChange) ;
    // on resuit l'unité de température (°C/°F) selon la locale OpenRGB.
    if (e && e->type() == QEvent::LanguageChange) {
        m_fahrenheit = OpenRGBSettings::prefersFahrenheit();
        for (ChannelPanel* p : m_fanPanels)
            p->editor->setFahrenheit(m_fahrenheit);
    }
}

// ─── Persistence ──────────────────────────────────────────────────────────────
// Même emplacement que le plugin LCD : %APPDATA%/OpenRGB/NZXTKrakenPump/
QString PumpWidget::configFilePath()
{
    return OpenRGBSettings::configDir()
           + QStringLiteral("/NZXTKrakenPump/settings.json");
}

static QJsonArray pointsToJson(const QVector<QPoint>& pts)
{
    QJsonArray arr;
    for (const QPoint& p : pts) {
        QJsonArray pair;
        pair.append(p.x());
        pair.append(p.y());
        arr.append(pair);
    }
    return arr;
}

static QVector<QPoint> pointsFromJson(const QJsonArray& arr)
{
    QVector<QPoint> pts;
    for (const auto& v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            pts.append(QPoint(pair[0].toInt(), pair[1].toInt()));
    }
    return pts;
}

void PumpWidget::saveConfig()
{
    // Tous les canaux (pompe comprise, clé "kraken/pump") dans "fans"
    QJsonObject fansObj;
    for (auto it = m_fanCfgs.constBegin(); it != m_fanCfgs.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("mode")]   = it.value().mode;
        o[QStringLiteral("source")] = it.value().sourceId;
        o[QStringLiteral("rise")]   = it.value().riseRate;
        o[QStringLiteral("fall")]   = it.value().fallRate;
        o[QStringLiteral("fixed")]  = it.value().fixedDuty;
        o[QStringLiteral("curve")]  = pointsToJson(it.value().points);
        if (!it.value().name.isEmpty())
            o[QStringLiteral("name")] = it.value().name;
        fansObj[it.key()] = o;
    }
    QJsonObject rootObj;
    rootObj[QStringLiteral("v")]    = 3;   // v3 : Auto retiré, Fixed ajouté
    rootObj[QStringLiteral("fans")] = fansObj;
    rootObj[QStringLiteral("dead")] = QJsonArray::fromStringList(m_deadIds);
    rootObj[QStringLiteral("pumpTach")] = QJsonArray::fromStringList(m_pumpTachIds);
    QJsonObject calibObj;   // table (duty, rpm) par canal (bouton Calibrate fans)
    for (auto it = m_calibTables.constBegin(); it != m_calibTables.constEnd(); ++it)
        calibObj[it.key()] = pointsToJson(it.value());
    rootObj[QStringLiteral("calib")] = calibObj;

    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[KrakenPump/Persistence] cannot write" << path;
        return;
    }
    f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qWarning() << "[KrakenPump/Persistence] commit failed for" << path;
}

void PumpWidget::loadConfig()
{
    QFile f(configFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject rootObj = doc.object();

    // v1 : Normal présent (pompe 0..3, ventilos 0..4 avec Auto) ;
    // v2 : Normal retiré (pompe 0..2, ventilos 0..3 avec Auto) ;
    // v3 : Auto retiré, Fixed inséré (tous : Silent/Perf/Fixed/Custom).
    const int ver = rootObj[QStringLiteral("v")].toInt(1);

    // Headers vides mémorisés (masqués au démarrage, surveillance continue)
    for (const auto& v : rootObj[QStringLiteral("dead")].toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty()) m_deadIds.append(id);
    }
    // Headers identifiés comme tach de la pompe (masqués au démarrage)
    for (const auto& v : rootObj[QStringLiteral("pumpTach")].toArray()) {
        const QString id = v.toString();
        if (!id.isEmpty()) m_pumpTachIds.append(id);
    }
    // Tables de calibration (bouton Calibrate fans) — base du % réel affiché.
    // Rétro-compat : ancien format = un seul entier (RPM max) -> table 2 points.
    const QJsonObject calibObj = rootObj[QStringLiteral("calib")].toObject();
    for (auto it = calibObj.constBegin(); it != calibObj.constEnd(); ++it) {
        if (it.value().isArray()) {
            const QVector<QPoint> tbl = pointsFromJson(it.value().toArray());
            if (tbl.size() >= 2) m_calibTables.insert(it.key(), tbl);
        } else {
            const int rpm = it.value().toInt(0);
            if (rpm > 0)
                m_calibTables.insert(it.key(),
                    { QPoint(100, rpm), QPoint(0, 0) });
        }
    }

    const QJsonObject fansObj = rootObj[QStringLiteral("fans")].toObject();
    for (auto it = fansObj.constBegin(); it != fansObj.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        FanChannelConfig cfg;
        const bool isPump = (it.key() == KRAKEN_PUMP_ID);
        int m = o[QStringLiteral("mode")].toInt(int(FanSilent));
        if (ver < 2) {
            // Silent/Normal -> Silent ; les index suivants reculent d'un cran
            if (isPump) m = (m <= 1) ? 0 : m - 1;
            else        m = (m <= 1) ? m : (m == 2 ? 1 : std::min(m - 1, 3));
        }
        if (ver < 3) {
            // Auto -> Silent (ventilos, index -1) ; Custom décalé par Fixed
            if (isPump) m = (m >= 2) ? 3 : m;
            else        m = (m <= 1) ? 0 : (m == 2 ? 1 : 3);
        }
        cfg.mode     = std::clamp(m, 0, 3);
        cfg.sourceId = o[QStringLiteral("source")].toString(
                           isPump ? QStringLiteral("liquid") : QString());
        cfg.fixedDuty = std::clamp(o[QStringLiteral("fixed")].toInt(50), 0, 100);
        if (ver >= 2 || o.contains(QStringLiteral("rise"))) {
            cfg.riseRate = std::clamp(o[QStringLiteral("rise")].toInt(5), 1, 100);
            cfg.fallRate = std::clamp(o[QStringLiteral("fall")].toInt(5), 1, 100);
        } else if (o.contains(QStringLiteral("fall"))) {
            // Migration v1 : ancien "fall" en secondes (temps 100 -> 0 %)
            const int sec = o[QStringLiteral("fall")].toInt(30);
            cfg.fallRate  = (sec <= 0) ? 100
                          : std::clamp(int(std::lround(100.0 / sec)), 1, 100);
        }
        cfg.points   = pointsFromJson(o[QStringLiteral("curve")].toArray());
        cfg.name     = o[QStringLiteral("name")].toString();
        if (cfg.points.size() < 2)
            cfg.points = isPump ? pumpPresetPoints(FanSilent)
                                : fanPresetPoints(FanSilent);
        m_fanCfgs.insert(it.key(), cfg);
    }

    // Migration : très ancien format avec la pompe à la racine ("mode"/"curve")
    if (!m_fanCfgs.contains(KRAKEN_PUMP_ID) && rootObj.contains(QStringLiteral("mode"))) {
        FanChannelConfig cfg;
        int m = rootObj[QStringLiteral("mode")].toInt(0);
        m = (m <= 1) ? 0 : (m == 2 ? 1 : 3);   // v1 pompe -> v3
        cfg.mode     = m;
        cfg.sourceId = QStringLiteral("liquid");
        cfg.points   = pointsFromJson(rootObj[QStringLiteral("curve")].toArray());
        if (cfg.points.size() < 2)
            cfg.points = pumpPresetPoints(FanSilent);
        m_fanCfgs.insert(KRAKEN_PUMP_ID, cfg);
    }
}

} // namespace NZXTKrakenPump
