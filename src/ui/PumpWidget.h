#pragma once
#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QElapsedTimer>
#include "../device/KrakenPumpDevice.h"
#include "PumpWorker.h"

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

class PumpWidget : public QWidget {
    Q_OBJECT
public:
    explicit PumpWidget(QWidget* parent = nullptr);
    ~PumpWidget() override;
    void setDarkTheme(bool dark);

protected:
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    enum IconType { IconPump = 0, IconFan, IconGpu };

    struct ChannelPanel {
        QString          id;
        QString          defaultName;
        bool             isPump  = false;
        int              iconType = IconFan;
        QWidget*         box     = nullptr;
        QLabel*          icon    = nullptr;
        QLineEdit*       name    = nullptr;
        QLabel*          reading = nullptr;   // "2273 RPM  76%"
        QLabel*          warn    = nullptr;
        QComboBox*       source  = nullptr;
        QComboBox*       mode     = nullptr;
        QWidget*         body     = nullptr;
        QSpinBox*        stepUp   = nullptr;
        QSpinBox*        stepDown = nullptr;
        QSpinBox*        zeroDuty = nullptr;
        PumpCurveEditor* editor   = nullptr;
    };

    void buildUI();
    ChannelPanel* createPanel(const QString& id, const QString& title, bool isPump);
    void rebuildFanPanels(const QStringList& ids, const QStringList& names,
                          const QStringList& groups);
    void fillSourceCombo(QComboBox* combo, const QString& selectedId);
    void setPanelExpanded(ChannelPanel* p, bool expanded);
    QFrame* makeSeparator();

    void applyPumpCfgToUi();
    void onPumpModeChanged();
    void onPumpCurveEdited();
    QVector<QPoint> effectivePumpPoints(const FanChannelConfig& cfg) const;
    void dispatchPump();

    FanChannelConfig& fanCfg(const QString& id);
    QVector<QPoint>   effectiveFanPoints(const FanChannelConfig& cfg) const;
    void refreshPanelEditor(ChannelPanel* p);
    void pushFanCfg(ChannelPanel* p);
    void onFanModeChanged(ChannelPanel* p);
    void onFanCurveEdited(ChannelPanel* p);
    void resetPanel(ChannelPanel* p);
    void showApplyPopover(ChannelPanel* src, QPushButton* anchor);
    void applyPanelCfgToUi(ChannelPanel* p);
    void startIdentify(ChannelPanel* p);
    void applyFaultStyle(ChannelPanel* p);

    void loadConfig();
    void saveConfig();
    static QString configFilePath();

    KrakenPumpDevice m_device;
    QThread*         m_thread = nullptr;
    PumpWorker*      m_worker = nullptr;

    // ── UI ──────────────────────────────────────────────────────────────────
    QLabel*       m_lblDeviceName = nullptr;
    QLabel*       m_lblStatus     = nullptr;
    QLabel*       m_updateBanner  = nullptr;
    class UpdateChecker* m_updater = nullptr;
    QPushButton*  m_btnCalib      = nullptr;
    QWidget*      m_calibBadge    = nullptr;

    void setCalibBadge(bool on);
    void positionCalibBadge();
    QVBoxLayout*  m_listLay       = nullptr;
    ChannelPanel* m_pumpPanel     = nullptr;
    QVector<ChannelPanel*> m_fanPanels;
    QVector<QFrame*>       m_groupFrames;
    QVector<QFrame*>       m_separators;
    QVector<QPushButton*>  m_hideBtns;
    QMap<QObject*, ChannelPanel*> m_nameOwner;
    QMap<QObject*, ChannelPanel*> m_boxOwner;
    QMap<QObject*, ChannelPanel*> m_iconOwner;

    struct IdentSpin {
        qint64 startMs     = 0;
        qreal  angle       = 0.0;
        qreal  decelStart  = -1.0;
        qreal  decelTarget = 0.0;
    };
    QMap<QString, IdentSpin> m_identSpin;

    struct CalibSpin {
        qreal angle     = 0.0;
        qreal vel       = 0.0;
        qreal targetVel = 0.0;
        bool  stopping  = false;
    };
    QMap<QString, CalibSpin> m_calibSpin;
    void ensureSpinTimer();

    QTimer*       m_spinTimer = nullptr;
    QElapsedTimer m_uiClock;
    QString m_krakenGroup;

    QMap<QString, FanChannelConfig> m_fanCfgs;
    QMap<QString, QString> m_faultReason;
    QStringList m_hiddenIds;
    bool        m_hiddenExpanded = false;
    QStringList m_pumpTachIds;
    QMap<QString, QVector<QPoint>> m_calibTables;
    QStringList m_srcIds;
    QStringList m_srcNames;
    QVector<float> m_srcVals;

    QStringList m_chIds, m_chNames, m_chGroups;

    QTimer* m_saveTimer = nullptr;

    bool m_darkTheme  = true;
    bool m_fahrenheit = false;
    bool m_ready      = false;
};

}
