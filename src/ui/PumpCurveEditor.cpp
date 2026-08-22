#include "PumpCurveEditor.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPalette>
#include <algorithm>
#include <cmath>

namespace NZXTKrakenPump {

// Accent violet de la charte du plugin LCD (bouton Rotate : #AA44FF)
static const QColor ACCENT(0xAA, 0x44, 0xFF);

PumpCurveEditor::PumpCurveEditor(QWidget* parent)
    : QWidget(parent)
{
    m_pts = { QPoint(20, 60), QPoint(45, 100) };
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
}

void PumpCurveEditor::setPoints(const QVector<QPoint>& pts)
{
    m_pts.clear();
    for (const QPoint& p : pts) {
        m_pts.append(QPoint(std::clamp(p.x(), TEMP_MIN, TEMP_MAX),
                            std::clamp(p.y(), m_minDuty, 100)));
    }
    std::stable_sort(m_pts.begin(), m_pts.end(),
                     [](const QPoint& a, const QPoint& b) { return a.x() < b.x(); });
    // Températures identiques (anciennes configs) : décaler de +1 °C plutôt
    // que supprimer — pas de perte de points ; on ne retire qu'en butée d'axe.
    for (int i = 1; i < m_pts.size(); ++i)
        if (m_pts[i].x() <= m_pts[i - 1].x())
            m_pts[i].setX(std::min(m_pts[i - 1].x() + 1, TEMP_MAX));
    for (int i = m_pts.size() - 1; i > 0; --i)
        if (m_pts[i].x() <= m_pts[i - 1].x())
            m_pts.removeAt(i - 1);
    if (m_pts.isEmpty())
        m_pts = { QPoint(20, std::max(60, m_minDuty)), QPoint(45, 100) };
    m_dragging = -1;
    m_originY.clear();
    update();
}

void PumpCurveEditor::setMinDuty(int minDuty)
{
    m_minDuty = std::clamp(minDuty, 0, 100);
    for (QPoint& p : m_pts)
        p.setY(std::max(p.y(), m_minDuty));
    update();
}

void PumpCurveEditor::setLiveTemp(float tempC)
{
    if (std::abs(tempC - m_liveTemp) < 0.05f) return;
    m_liveTemp = tempC;
    update();
}

void PumpCurveEditor::setFahrenheit(bool on)
{
    if (on == m_fahrenheit) return;
    m_fahrenheit = on;
    update();
}

QRectF PumpCurveEditor::plotRect() const
{
    // Colonne de gauche compacte (libellés %) ; marge droite paramétrable
    // (72 = symétrique, réduite quand des contrôles occupent la droite).
    return QRectF(46, 10, width() - 46 - m_marginRight, height() - 48);
}

QPointF PumpCurveEditor::toWidget(const QPoint& pt) const
{
    const QRectF r  = plotRect();
    const qreal  fx = qreal(pt.x() - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    const qreal  fy = qreal(pt.y()) / 100.0;
    return { r.left() + fx * r.width(), r.bottom() - fy * r.height() };
}

QPoint PumpCurveEditor::fromWidget(const QPointF& w) const
{
    const QRectF r = plotRect();
    const int temp = int(std::lround(TEMP_MIN
                     + (w.x() - r.left()) / r.width() * (TEMP_MAX - TEMP_MIN)));
    const int duty = int(std::lround((r.bottom() - w.y()) / r.height() * 100.0));
    return { std::clamp(temp, TEMP_MIN, TEMP_MAX),
             std::clamp(duty, m_minDuty, 100) };
}

int PumpCurveEditor::handleAt(const QPoint& pos) const
{
    int best = -1;
    qreal bestDist = 12.0;   // rayon de capture
    for (int i = 0; i < m_pts.size(); ++i) {
        const QPointF p = toWidget(m_pts[i]);
        const qreal d = std::hypot(p.x() - pos.x(), p.y() - pos.y());
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void PumpCurveEditor::dragTo(int index, const QPoint& mouse)
{
    QPoint p = fromWidget(mouse);

    // Mode Fixed : 2 points liés au même duty, X verrouillé (0° / 100°).
    if (m_fixedMode && m_pts.size() == 2) {
        const int y = p.y();
        if (m_pts[0].y() == y && m_pts[1].y() == y) return;
        m_pts[0].setY(y);
        m_pts[1].setY(y);
        update();
        emit curveChanged();
        return;
    }

    // Température (X) : BLOQUÉE par les voisins (1 °C d'écart minimum) —
    // les points restent triés, pas de poussée sur cet axe.
    if (index > 0)
        p.setX(std::max(p.x(), m_pts[index - 1].x() + 1));
    if (index < m_pts.size() - 1)
        p.setX(std::min(p.x(), m_pts[index + 1].x() - 1));

    bool changed = (m_pts[index] != p);
    m_pts[index] = p;

    // Duty (Y) : POUSSÉE avec mémoire de geste — chaque voisin vaut
    // min/max(son duty au début du drag, duty du point manipulé) : il suit
    // quand on le traverse, et reprend sa position d'origine (jamais plus)
    // si on repart en arrière SANS relâcher. Au relâchement, les positions
    // courantes deviennent les nouvelles origines du prochain geste.
    const bool hasOrigin = (m_originY.size() == m_pts.size());
    for (int i = index - 1; i >= 0; --i) {
        const int base = hasOrigin ? m_originY[i] : m_pts[i].y();
        const int y    = std::min(base, p.y());
        if (m_pts[i].y() != y) { m_pts[i].setY(y); changed = true; }
    }
    for (int i = index + 1; i < m_pts.size(); ++i) {
        const int base = hasOrigin ? m_originY[i] : m_pts[i].y();
        const int y    = std::max(base, p.y());
        if (m_pts[i].y() != y) { m_pts[i].setY(y); changed = true; }
    }

    if (!changed) return;
    update();
    emit curveChanged();
}

void PumpCurveEditor::mousePressEvent(QMouseEvent* e)
{
    const int hit = handleAt(e->pos());

    if (e->button() == Qt::RightButton) {
        // Clic droit : suppression (toujours garder au moins 2 points)
        if (hit >= 0 && m_pts.size() > 2) {
            m_pts.removeAt(hit);
            m_dragging = -1;
            update();
            emit curveChanged();
        }
        return;
    }

    if (e->button() != Qt::LeftButton) return;

    // Origines du geste : duty de chaque point à l'instant du clic
    const auto snapshotOrigins = [this] {
        m_originY.resize(m_pts.size());
        for (int i = 0; i < m_pts.size(); ++i)
            m_originY[i] = m_pts[i].y();
    };

    if (hit >= 0) {
        m_dragging = hit;
        snapshotOrigins();
        dragTo(m_dragging, e->pos());
        return;
    }

    // Clic gauche dans le vide : ajout d'un point (si la température est libre)
    const QRectF r = plotRect();
    if (!r.adjusted(-10, -10, 10, 10).contains(e->pos()))
        return;
    const QPoint p = fromWidget(e->pos());
    for (const QPoint& q : m_pts)
        if (q.x() == p.x()) return;   // température déjà occupée

    int insertAt = m_pts.size();
    for (int i = 0; i < m_pts.size(); ++i)
        if (m_pts[i].x() > p.x()) { insertAt = i; break; }
    m_pts.insert(insertAt, p);
    m_dragging = insertAt;
    snapshotOrigins();
    // Pousser immédiatement les voisins au niveau du nouveau point (la courbe
    // doit rester monotone dès le clic, pas seulement au premier mouvement).
    for (int i = insertAt - 1; i >= 0; --i)
        if (m_pts[i].y() > p.y()) m_pts[i].setY(p.y());
    for (int i = insertAt + 1; i < m_pts.size(); ++i)
        if (m_pts[i].y() < p.y()) m_pts[i].setY(p.y());
    update();
    emit curveChanged();
}

void PumpCurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging >= 0)
        dragTo(m_dragging, e->pos());
}

void PumpCurveEditor::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = -1;
    m_originY.clear();   // les positions courantes deviennent les origines
}

void PumpCurveEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = plotRect();

    // Couleurs dérivées de la palette de l'application (OpenRGB sombre/clair),
    // même approche que LCDPreviewWidget du plugin LCD.
    const QColor plotBg = palette().color(QPalette::Base);
    QColor grid = palette().color(QPalette::Mid);
    grid.setAlpha(110);
    QColor text = palette().color(QPalette::WindowText);
    text.setAlpha(170);

    p.fillRect(r, plotBg);

    // Grille + libellés
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    for (int duty = 0; duty <= 100; duty += 20) {
        const qreal y = r.bottom() - (duty / 100.0) * r.height();
        p.setPen(grid);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(text);
        p.drawText(QRectF(r.left() - 34, y - 7, 28, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(duty) + QStringLiteral("%"));
    }
    for (int t = TEMP_MIN; t <= TEMP_MAX; t += 10) {
        const qreal x = r.left()
            + qreal(t - TEMP_MIN) / (TEMP_MAX - TEMP_MIN) * r.width();
        p.setPen(grid);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        const int shown = m_fahrenheit ? t * 9 / 5 + 32 : t;
        p.setPen(text);
        p.drawText(QRectF(x - 16, r.bottom() + 4, 32, 14), Qt::AlignCenter,
                   QString::number(shown) + QString(QChar(0x00B0)));
    }

    if (m_pts.isEmpty()) return;

    // Courbe : plateau avant le premier point, segments, plateau après le
    // dernier (même sémantique que l'interpolation envoyée au device).
    QPainterPath curve;
    curve.moveTo(QPointF(r.left(), toWidget(m_pts.first()).y()));
    for (const QPoint& pt : m_pts)
        curve.lineTo(toWidget(pt));
    curve.lineTo(QPointF(r.right(), toWidget(m_pts.last()).y()));

    QPainterPath fill = curve;
    fill.lineTo(r.right(), r.bottom());
    fill.lineTo(r.left(), r.bottom());
    fill.closeSubpath();
    QColor fillCol = ACCENT;
    fillCol.setAlpha(45);
    p.fillPath(fill, fillCol);

    p.setPen(QPen(ACCENT, 2));
    p.drawPath(curve);

    // Curseur température live
    if (m_liveTemp >= TEMP_MIN) {
        const qreal fx = std::clamp(qreal(m_liveTemp - TEMP_MIN)
                                    / (TEMP_MAX - TEMP_MIN), qreal(0), qreal(1));
        const qreal x = r.left() + fx * r.width();
        p.setPen(QPen(QColor(255, 190, 60, 180), 1, Qt::DashLine));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }

    // Guides du point en cours de déplacement : lignes verticale + horizontale
    // et valeurs (% à gauche, ° en bas) au niveau du point.
    if (m_dragging >= 0 && m_dragging < m_pts.size()) {
        const QPoint  cur = m_pts[m_dragging];
        const QPointF w   = toWidget(cur);

        p.setPen(QPen(ACCENT, 1, Qt::DashLine));
        p.drawLine(QPointF(r.left(), w.y()), QPointF(w.x(), w.y()));
        p.drawLine(QPointF(w.x(), w.y()), QPointF(w.x(), r.bottom()));

        QFont bf = p.font();
        bf.setBold(true);
        p.setFont(bf);

        // Valeur % : même colonne que les libellés fixes, sur une pastille de
        // fond pour masquer un éventuel libellé de grille en dessous.
        {
            const QRectF pr(6, w.y() - 8, 38, 16);
            p.setPen(Qt::NoPen);
            p.setBrush(plotBg);
            p.drawRoundedRect(pr, 3, 3);
            p.setPen(ACCENT);
            p.drawText(pr.adjusted(0, 0, -4, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(cur.y()) + QStringLiteral("%"));
        }

        // Température : rangée dédiée SOUS les degrés fixes de l'axe
        const int shown = m_fahrenheit ? cur.x() * 9 / 5 + 32 : cur.x();
        p.drawText(QRectF(w.x() - 16, r.bottom() + 20, 32, 14),
                   Qt::AlignCenter,
                   QString::number(shown) + QString(QChar(0x00B0)));

        bf.setBold(false);
        p.setFont(bf);
    }

    // Poignées : contour noir en thème sombre, blanc (BrightText) en clair
    const bool   darkTheme = palette().color(QPalette::Window).lightness() < 128;
    const QColor handleBorder = darkTheme ? QColor(Qt::black)
                                          : palette().color(QPalette::BrightText);
    for (int i = 0; i < m_pts.size(); ++i) {
        const QPointF pt = toWidget(m_pts[i]);
        p.setPen(QPen(handleBorder, 1.5));
        p.setBrush(i == m_dragging ? ACCENT.lighter(130) : ACCENT);
        p.drawEllipse(pt, 5.5, 5.5);
    }
}

} // namespace NZXTKrakenPump
