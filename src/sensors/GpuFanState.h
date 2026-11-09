#pragma once
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>

namespace NZXTKrakenPump {

constexpr int kAdlxFailsafeTempC = 65;

inline QVector<QPoint> adlxTable(const QVector<QPoint>& orig, int speed)
{
    QVector<QPoint> out;
    out.reserve(orig.size());
    for (const QPoint& o : orig)
        out.append(QPoint(-1, o.x() >= kAdlxFailsafeTempC ? std::max(speed, o.y()) : speed));
    return out;
}

inline bool adlxLooksLikePluginTable(const QVector<QPoint>& cur, const QVector<QPoint>& orig)
{
    if (cur.isEmpty() || cur.size() != orig.size()) return false;
    int flat = -1;
    for (int i = 0; i < cur.size(); ++i) {
        if (cur[i].x() != orig[i].x()) return false;
        if (orig[i].x() >= kAdlxFailsafeTempC) {
            if (cur[i].y() < orig[i].y()) return false;
        } else if (flat < 0) {
            flat = cur[i].y();
        } else if (cur[i].y() != flat) {
            return false;
        }
    }
    return true;
}

inline QByteArray adlxSerialize(const QVector<QPoint>& pts, bool zeroRpm)
{
    QByteArray b;
    for (const QPoint& p : pts)
        b += QByteArray::number(p.x()) + ' ' + QByteArray::number(p.y()) + '\n';
    b += QByteArray("zero ") + (zeroRpm ? "1" : "0") + '\n';
    return b;
}

inline bool adlxParse(const QByteArray& data, QVector<QPoint>& pts, bool& zeroRpm)
{
    pts.clear();
    bool haveZero = false;
    for (const QByteArray& line : data.split('\n')) {
        const QList<QByteArray> f = line.trimmed().split(' ');
        if (f.size() != 2) { if (line.trimmed().isEmpty()) continue; return false; }
        bool a = false, b = false;
        if (f[0] == "zero") { zeroRpm = (f[1].toInt(&a) != 0); if (!a) return false; haveZero = true; continue; }
        const int t = f[0].toInt(&a), s = f[1].toInt(&b);
        if (!a || !b || t < 0 || t > 200 || s < 0 || s > 100) return false;
        pts.append(QPoint(t, s));
    }
    return haveZero && !pts.isEmpty();
}

inline QString& gpuFanStateDirRef() { static QString d; return d; }
inline QString gpuFanStateDir()
{
    QString& d = gpuFanStateDirRef();
    if (d.isEmpty())
        d = qEnvironmentVariable("APPDATA") + QStringLiteral("/OpenRGB/NZXTKrakenPump");
    return d;
}
inline void setGpuFanStateDir(const QString& dir) { gpuFanStateDirRef() = dir; }

inline QString gpuFanStatePath(const QString& base, const QString& tag = QString())
{
    QString safe = tag;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("_"));
    return gpuFanStateDir() + QLatin1Char('/') + base
         + (safe.isEmpty() ? QString() : QLatin1Char('_') + safe) + QStringLiteral(".txt");
}
inline bool gpuFanStateWrite(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

}
