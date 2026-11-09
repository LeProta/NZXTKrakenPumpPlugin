#include <QtTest>
#include "fakes.h"
#include "ui/PumpWorker.h"

using namespace NZXTKrakenPump;

static const QString kCpu  = QStringLiteral("/amdcpu/0/temperature/2");
static const QString kFan1 = QStringLiteral("/lpc/nct6799d/0/control/0");

static FanChannelConfig curve(const QString& src, const QVector<QPoint>& pts,
                              int rise = 100, int fall = 100)
{
    FanChannelConfig c;
    c.mode = FanCustom;
    c.sourceId = src;
    c.points = pts;
    c.riseRate = rise;
    c.fallRate = fall;
    return c;
}

static void tick(PumpWorker& w, int n = 1)
{
    for (int i = 0; i < n; ++i)
        QMetaObject::invokeMethod(&w, "tick", Qt::DirectConnection);
}

static int lastDutyFor(const QString& id)
{
    for (int i = fake::lhwm.setDutyCalls.size() - 1; i >= 0; --i)
        if (fake::lhwm.setDutyCalls[i].first == id) return fake::lhwm.setDutyCalls[i].second;
    return -1;
}

class TestPumpWorker : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        fake::reset();
        fake::dev.readings.liquidTemp = 30.f;
        fake::lhwm.channels = { { kFan1, QStringLiteral("/lpc/nct6799d/0/fan/0"),
                                  QStringLiteral("Fan #1"), QStringLiteral("MB") } };
        fake::lhwm.tempIds   = QStringList{ kCpu };
        fake::lhwm.tempNames = QStringList{ QStringLiteral("CPU Average") };
        fake::lhwm.values[kCpu] = 50.f;
    }

    void interpolatesCurve()
    {
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(kFan1, curve(kCpu, { {30, 20}, {70, 100} }));
        tick(w);
        QCOMPARE(lastDutyFor(kFan1), 60);
    }

    void riseRateLimitsStep()
    {
        fake::lhwm.values[kCpu] = 30.f;
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(kFan1, curve(kCpu, { {0, 20}, {40, 20}, {41, 100}, {100, 100} }, 5, 5));
        tick(w);
        QCOMPARE(lastDutyFor(kFan1), 20);
        fake::lhwm.values[kCpu] = 90.f;
        tick(w);
        QCOMPARE(lastDutyFor(kFan1), 25);
    }

    void pumpFloorAndLiquidSafetyTable()
    {
        fake::dev.readings.pumpDuty = 20;
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(KRAKEN_PUMP_ID, curve(QStringLiteral("liquid"), { {20, 0}, {60, 0} }));
        tick(w);
        QCOMPARE(fake::dev.pumpCurves.size(), 1);
        const auto& t = fake::dev.pumpCurves.first();
        QCOMPARE(int(t[0]), 20);
        QCOMPARE(int(t[49]), 20);
        QCOMPARE(int(t[50]), 100);
        QCOMPARE(int(t[59]), 100);
    }

    void hotLiquidDoesNotForceRewriteEveryTick()
    {
        fake::dev.readings.liquidTemp = 55.f;
        fake::dev.readings.pumpDuty   = 100;
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(KRAKEN_PUMP_ID, curve(QStringLiteral("liquid"), { {20, 30}, {60, 30} }));
        tick(w, 4);
        QCOMPARE(fake::dev.pumpCurves.size(), 1);
    }

    void reportedDutyDriftStillForcesRewrite()
    {
        fake::dev.readings.pumpDuty = 30;
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(KRAKEN_PUMP_ID, curve(QStringLiteral("liquid"), { {20, 30}, {60, 30} }));
        tick(w, 2);
        QCOMPARE(fake::dev.pumpCurves.size(), 1);
        fake::dev.readings.pumpDuty = 0;
        tick(w);
        QCOMPARE(fake::dev.pumpCurves.size(), 2);
    }

    void failedSetDutyIsRetried()
    {
        fake::lhwm.failSetDuty = 1;
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(kFan1, curve(kCpu, { {0, 40}, {100, 40} }));
        tick(w, 3);
        QCOMPARE(fake::lhwm.setDutyCalls.size(), 2);
        QCOMPARE(lastDutyFor(kFan1), 40);
    }

    void invalidSensorReturnsChannelToBios()
    {
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        QSignalSpy faults(&w, &PumpWorker::fanSensorFault);
        w.setFanConfig(kFan1, curve(kCpu, { {0, 40}, {100, 40} }));
        tick(w);
        fake::lhwm.values[kCpu] = 0.f;
        tick(w, 7);
        QVERIFY(fake::lhwm.setDefaultCalls.isEmpty());
        tick(w);
        QCOMPARE(fake::lhwm.setDefaultCalls, QStringList{ kFan1 });
        QCOMPARE(faults.size(), 1);
    }

    void liquidSourceLossReturnsMotherboardFanToBios()
    {
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        QSignalSpy faults(&w, &PumpWorker::fanSensorFault);
        w.setFanConfig(kFan1, curve(QStringLiteral("liquid"), { {0, 40}, {100, 40} }));
        tick(w);
        QCOMPARE(lastDutyFor(kFan1), 40);
        fake::dev.readOk  = false;
        fake::dev.present = false;
        tick(w, 12);
        QCOMPARE(fake::lhwm.setDefaultCalls, QStringList{ kFan1 });
        QVERIFY(!faults.isEmpty());
        QVERIFY(faults.last().at(1).toStringList().value(0).contains(QStringLiteral("liquid")));
    }

    void stopReturnsManagedChannelsToBios()
    {
        KrakenPumpDevice d; PumpWorker w(&d); w.start();
        w.setFanConfig(kFan1, curve(kCpu, { {0, 40}, {100, 40} }));
        tick(w);
        w.stop();
        QCOMPARE(fake::lhwm.setDefaultCalls, QStringList{ kFan1 });
        QVERIFY(!fake::dev.isOpen);
    }
};

QTEST_GUILESS_MAIN(TestPumpWorker)
#include "tst_pumpworker.moc"
