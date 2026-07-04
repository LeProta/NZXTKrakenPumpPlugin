#pragma once
#include <QWidget>
#include <QVector>
#include <QPoint>

namespace NZXTKrakenPump {

// ─────────────────────────────────────────────────────────────────────────────
// PumpCurveEditor
//   Éditeur de courbe température liquide -> duty, style NZXT CAM :
//   points libres (x = °C, y = %), déplaçables en X et Y.
//     • clic gauche sur un point : déplacement (drag) ;
//     • clic gauche dans le vide : ajout d'un point ;
//     • clic droit sur un point  : suppression (minimum 2 points).
//   Couleurs dérivées de la palette OpenRGB (thème clair/sombre suivi
//   automatiquement) + accent violet #AA44FF (charte du plugin LCD).
//   L'axe s'affiche en °F quand la locale OpenRGB le demande (les valeurs
//   internes restent en °C, seule l'ÉTIQUETTE est convertie).
//   curveChanged() n'est émis QUE pour une édition utilisateur — jamais par
//   setPoints() : le widget parent s'en sert pour basculer en "Custom curve".
// ─────────────────────────────────────────────────────────────────────────────
class PumpCurveEditor : public QWidget {
    Q_OBJECT
public:
    // Axe affiché : 0..100 °C. Le protocole 0x72 n'échantillonne que 20..59 °C
    // (l'interpolation vers les 40 duties est faite par le widget parent).
    static constexpr int TEMP_MIN = 0;
    static constexpr int TEMP_MAX = 100;

    explicit PumpCurveEditor(QWidget* parent = nullptr);

    // Points (x = °C trié croissant, y = duty). Clampés et triés à l'entrée.
    void            setPoints(const QVector<QPoint>& pts);
    QVector<QPoint> points() const { return m_pts; }

    void setMinDuty(int minDuty);      // 20 pour la pompe
    void setLiveTemp(float tempC);     // curseur température live (<0 = caché)
    void setFahrenheit(bool on);       // étiquettes d'axe seulement
    // Mode "Fixed" : 2 points (0° et 100°) au même duty, liés — en déplacer un
    // déplace l'autre, X verrouillé. L'ajout d'un point reste possible (le
    // widget parent bascule alors en Custom).
    void setFixedMode(bool on) { m_fixedMode = on; }
    // Marge droite du tracé (72 par défaut, symétrique). Réduite quand une
    // colonne de contrôles occupe la droite du graphe.
    void setRightMargin(int px) { m_marginRight = px; update(); }

signals:
    void curveChanged();   // édition utilisateur (drag/ajout/retrait)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize minimumSizeHint() const override { return { 340, 190 }; }

private:
    QRectF  plotRect() const;
    QPointF toWidget(const QPoint& pt) const;    // (°C, %) -> pixels
    QPoint  fromWidget(const QPointF& w) const;  // pixels -> (°C, %)
    int     handleAt(const QPoint& pos) const;
    void    dragTo(int index, const QPoint& mouse);

    QVector<QPoint> m_pts;
    QVector<int>    m_originY;   // duty de chaque point au début du geste :
                                 // pendant un drag, un voisin poussé revient
                                 // vers son origine si on repart en arrière
    int             m_marginRight = 72;
    int             m_minDuty    = 20;
    int             m_dragging   = -1;
    float           m_liveTemp   = -1.f;
    bool            m_fahrenheit = false;
    bool            m_fixedMode  = false;
};

} // namespace NZXTKrakenPump
