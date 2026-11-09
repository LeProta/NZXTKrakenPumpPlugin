#pragma once
#include <QWidget>
#include <QVector>
#include <QPoint>

namespace NZXTKrakenPump {

class PumpCurveEditor : public QWidget {
    Q_OBJECT
public:
    static constexpr int TEMP_MIN = 0;
    static constexpr int TEMP_MAX = 100;

    explicit PumpCurveEditor(QWidget* parent = nullptr);

    void            setPoints(const QVector<QPoint>& pts);
    QVector<QPoint> points() const { return m_pts; }

    void setMinDuty(int minDuty);
    void setLiveTemp(float tempC);
    void setFahrenheit(bool on);
    void setFixedMode(bool on) { m_fixedMode = on; }
    void setRightMargin(int px) { m_marginRight = px; update(); }

signals:
    void curveChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize minimumSizeHint() const override { return { 340, 190 }; }

private:
    QRectF  plotRect() const;
    QPointF toWidget(const QPoint& pt) const;
    QPoint  fromWidget(const QPointF& w) const;
    int     handleAt(const QPoint& pos) const;
    void    dragTo(int index, const QPoint& mouse);

    QVector<QPoint> m_pts;
    QVector<int>    m_originY;
    int             m_marginRight = 72;
    int             m_minDuty    = 20;
    int             m_dragging   = -1;
    float           m_liveTemp   = -1.f;
    bool            m_fahrenheit = false;
    bool            m_fixedMode  = false;
};

}
