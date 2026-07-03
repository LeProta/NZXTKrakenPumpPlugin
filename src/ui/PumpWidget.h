#pragma once
#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QElapsedTimer>
#include "../device/KrakenPumpDevice.h"
#include "PumpWorker.h"   // FanChannelConfig, FanMode, KRAKEN_FAN_ID, KRAKEN_PUMP_ID

class QLabel;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QFrame;
class QVBoxLayout;
class QThread;
class QTimer;
class QEvent;

namespace NZXTKrakenPump {

class PumpCurveEditor;

// ─── Widget principal ─────────────────────────────────────────────────────────
// Un BLOC par hardware (Kraken, carte mère, GPU) : cadre discret contenant le
// titre du groupe puis ses canaux séparés par une fine ligne (style NZXT CAM).
// Chaque canal : icône carrée (pompe/ventilo/GPU) + nom renommable + RPM/duty
// + sonde + mode, et une courbe repliable (repliée par défaut). Un clic
// n'importe où sur la rangée replie/déplie ; double-clic sur le nom = renommage.
class PumpWidget : public QWidget {
    Q_OBJECT
public:
    explicit PumpWidget(QWidget* parent = nullptr);
    ~PumpWidget() override;
    void setDarkTheme(bool dark);

protected:
    void changeEvent(QEvent* e) override;   // suit le thème + la langue d'OpenRGB
    bool eventFilter(QObject* obj, QEvent* e) override;   // clic rangée + renommage
    void resizeEvent(QResizeEvent* e) override;   // replace la pastille calib
    void showEvent(QShowEvent* e) override;

private:
    // Modes : FanMode (PumpWorker.h) partagé pompe/ventilos —
    // Silent / Performance / Fixed / Custom (config v3).
    enum IconType { IconPump = 0, IconFan, IconGpu };

    // Panneau d'un canal (pompe ou ventilateur)
    struct ChannelPanel {
        QString          id;
        QString          defaultName;         // nom d'origine (worker)
        bool             isPump  = false;
        int              iconType = IconFan;
        QWidget*         box     = nullptr;   // rangée cliquable + corps
        QLabel*          icon    = nullptr;
        QLineEdit*       name    = nullptr;   // renommable (double-clic)
        QLabel*          reading = nullptr;   // "2273 RPM  76%"
        QComboBox*       source  = nullptr;   // sonde, dans l'en-tête
        QComboBox*       mode     = nullptr;
        QWidget*         body     = nullptr;   // partie repliable (courbe)
        QSpinBox*        stepUp   = nullptr;   // montée max %/s (corps)
        QSpinBox*        stepDown = nullptr;   // descente max %/s (corps)
        PumpCurveEditor* editor   = nullptr;
    };

    void buildUI();
    ChannelPanel* createPanel(const QString& id, const QString& title, bool isPump);
    void rebuildFanPanels(const QStringList& ids, const QStringList& names,
                          const QStringList& groups);
    void fillSourceCombo(QComboBox* combo, const QString& selectedId);
    void setPanelExpanded(ChannelPanel* p, bool expanded);
    QFrame* makeSeparator();

    // ── Pompe ───────────────────────────────────────────────────────────────
    void applyPumpCfgToUi();                  // recharge le panneau pompe (signaux bloqués)
    void onPumpModeChanged();
    void onPumpCurveEdited();
    QVector<QPoint> effectivePumpPoints(const FanChannelConfig& cfg) const;
    void dispatchPump();                      // boucle logicielle (worker)

    // ── Ventilateurs ────────────────────────────────────────────────────────
    FanChannelConfig& fanCfg(const QString& id);      // config stockée (points = custom)
    QVector<QPoint>   effectiveFanPoints(const FanChannelConfig& cfg) const;
    void refreshPanelEditor(ChannelPanel* p);
    void pushFanCfg(ChannelPanel* p);                 // envoie la config effective au worker
    void onFanModeChanged(ChannelPanel* p);
    void onFanCurveEdited(ChannelPanel* p);
    void resetPanel(ChannelPanel* p);                 // mode/courbe/rampes par défaut
    void showApplyPopover(ChannelPanel* src, QPushButton* anchor);
    void applyPanelCfgToUi(ChannelPanel* p);          // recharge combos/spins/éditeur
    void startIdentify(ChannelPanel* p);              // clic sur l'icône ventilo

    void loadConfig();
    void saveConfig();
    static QString configFilePath();

    // ── Device / worker ─────────────────────────────────────────────────────
    KrakenPumpDevice m_device;
    QThread*         m_thread = nullptr;
    PumpWorker*      m_worker = nullptr;

    // ── UI ──────────────────────────────────────────────────────────────────
    QLabel*       m_lblDeviceName = nullptr;
    QLabel*       m_lblStatus     = nullptr;
    QPushButton*  m_btnCalib      = nullptr;   // "Calibrate fans" (en-tête)
    QWidget*      m_calibBadge    = nullptr;   // pastille ✓ overlay (coin du bouton)

    void setCalibBadge(bool on);               // affiche/masque + replace la pastille
    void positionCalibBadge();                 // centre la pastille sur le coin haut-droit
    QVBoxLayout*  m_listLay       = nullptr;   // colonne des blocs (dans le scroll)
    ChannelPanel* m_pumpPanel     = nullptr;   // aussi présent dans m_fanPanels
    QVector<ChannelPanel*> m_fanPanels;        // TOUS les panneaux (pompe comprise)
    QVector<QFrame*>       m_groupFrames;      // un bloc par hardware
    QVector<QFrame*>       m_separators;
    QVector<QPushButton*>  m_hideBtns;         // boutons ✕/Show/Hidden (restyle thème)
    QMap<QObject*, ChannelPanel*> m_nameOwner;   // QLineEdit -> panneau (renommage)
    QMap<QObject*, ChannelPanel*> m_boxOwner;    // rangée -> panneau (clic repli)
    QMap<QObject*, ChannelPanel*> m_iconOwner;   // icône -> panneau (identify)

    // Identify : pales de l'icône en rotation pendant les 5 s à 100 %,
    // avec accélération puis décélération progressives (phase par canal).
    struct IdentSpin {
        qint64 startMs     = 0;
        qreal  angle       = 0.0;
        qreal  decelStart  = -1.0;   // angle à l'entrée en décélération (<0 = pas encore)
        qreal  decelTarget = 0.0;    // angle final, multiple de 360/7 (pales alignées)
    };
    QMap<QString, IdentSpin> m_identSpin;
    QTimer*       m_spinTimer = nullptr;
    QElapsedTimer m_uiClock;
    QString m_krakenGroup;                       // titre du bloc Kraken

    QMap<QString, FanChannelConfig> m_fanCfgs;     // par identifiant (points = custom)
    QStringList m_hiddenIds;                       // canaux masqués manuellement (bouton ✕, persistés)
    bool        m_hiddenExpanded = false;          // section "Hidden fans" dépliée
    QStringList m_pumpTachIds;                     // headers = tach pompe (persistés)
    QMap<QString, QVector<QPoint>> m_calibTables;  // (duty, rpm) mesurés par canal
                                                   // (persisté) -> % réel = interp
                                                   // inverse RPM courant -> duty
    QStringList m_srcIds;                          // sources temp (ordre worker)
    QStringList m_srcNames;
    QVector<float> m_srcVals;                      // dernières valeurs des sources

    // Dernier état des canaux (pour reconstruire les blocs au changement)
    QStringList m_chIds, m_chNames, m_chGroups;

    QTimer* m_saveTimer = nullptr;    // debounce écriture settings.json

    bool m_darkTheme  = true;
    bool m_fahrenheit = false;        // suit la locale/langue OpenRGB
    bool m_ready      = false;        // true une fois le constructeur terminé
};

} // namespace NZXTKrakenPump
