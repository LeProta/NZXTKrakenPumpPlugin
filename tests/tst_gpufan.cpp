// Fonctions pures de GpuFanState.h : table de sécurité AMD, reconnaissance d'une
// table laissée par le plugin, fichier de récupération. Aucun GPU requis.
#include <QtTest>
#include "sensors/GpuFanState.h"

using namespace NZXTKrakenPump;

class TestGpuFan : public QObject {
    Q_OBJECT
    // Table d'origine mesurée sur la RX 9070 XT : (°C, %)
    const QVector<QPoint> stock { {30, 20}, {45, 30}, {55, 40}, {65, 55}, {80, 80} };

private slots:
    void flatBelowFailsafeStockAbove()
    {
        const auto t = adlxTable(stock, 20);
        QCOMPARE(t.size(), 5);
        QCOMPARE(t[0].y(), 20); QCOMPARE(t[1].y(), 20); QCOMPARE(t[2].y(), 20);
        QCOMPARE(t[3].y(), 55);                    // 65 °C : jamais sous l'origine
        QCOMPARE(t[4].y(), 80);                    // 80 °C : idem
        for (const QPoint& p : t) QCOMPARE(p.x(), -1);   // températures inchangées
    }
    void userSpeedAboveStockWins()
    {
        const auto t = adlxTable(stock, 90);       // l'utilisateur veut plus que l'origine
        for (const QPoint& p : t) QCOMPARE(p.y(), 90);
    }
    void recognisesItsOwnTable()
    {
        QVERIFY(adlxLooksLikePluginTable({ {30,42},{45,42},{55,42},{65,55},{80,80} }, stock));
        QVERIFY(adlxLooksLikePluginTable({ {30,20},{45,20},{55,20},{65,55},{80,80} }, stock));
        // L'origine elle-même (croissante) n'a pas la forme du plugin : AdlxFan::init la
        // traite à part (table identique à l'origine = déjà restaurée, rien à faire).
        QVERIFY(!adlxLooksLikePluginTable(stock, stock));
    }
    void rejectsUserCustomCurve()
    {
        // Courbe croissante posée dans Adrenalin : pas plate sous 65 °C -> on n'y touche pas.
        QVERIFY(!adlxLooksLikePluginTable({ {30,25},{45,35},{55,45},{65,60},{80,90} }, stock));
        // Températures modifiées.
        QVERIFY(!adlxLooksLikePluginTable({ {30,20},{50,20},{55,20},{65,55},{80,80} }, stock));
        // Au-dessus du seuil, sous l'origine : jamais écrit par nous.
        QVERIFY(!adlxLooksLikePluginTable({ {30,20},{45,20},{55,20},{65,30},{80,80} }, stock));
        // Taille différente / vide.
        QVERIFY(!adlxLooksLikePluginTable({ {30,20} }, stock));
        QVERIFY(!adlxLooksLikePluginTable({}, stock));
    }
    void recoveryFileRoundTrip()
    {
        bool z = false; QVector<QPoint> back;
        QVERIFY(adlxParse(adlxSerialize(stock, true), back, z));
        QCOMPARE(back, stock); QVERIFY(z);
        QVERIFY(adlxParse(adlxSerialize(stock, false), back, z));
        QVERIFY(!z);
    }
    void recoveryFileRejectsGarbage()
    {
        bool z = false; QVector<QPoint> out;
        QVERIFY(!adlxParse("", out, z));
        QVERIFY(!adlxParse("30 20\n", out, z));                    // pas de ligne zero
        QVERIFY(!adlxParse("zero 1\n", out, z));                   // pas de points
        QVERIFY(!adlxParse("30 20\nabc def\nzero 1\n", out, z));
        QVERIFY(!adlxParse("30 250\nzero 1\n", out, z));           // vitesse hors 0..100
        QVERIFY(!adlxParse("-5 20\nzero 1\n", out, z));
        QVERIFY(!adlxParse("30 20 7\nzero 1\n", out, z));
    }
    void statePathIsSanitised()
    {
        QTemporaryDir d; setGpuFanStateDir(d.path());
        const QString p = gpuFanStatePath(QStringLiteral("adlx_recovery"), QStringLiteral("AMD Radeon RX 9070 XT"));
        QVERIFY(p.endsWith(QStringLiteral("adlx_recovery_AMD_Radeon_RX_9070_XT.txt")));
        QVERIFY(gpuFanStateWrite(p, "x"));
        QVERIFY(QFile::exists(p));
    }
};

QTEST_APPLESS_MAIN(TestGpuFan)
#include "tst_gpufan.moc"
