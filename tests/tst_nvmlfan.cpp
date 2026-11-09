#include <QtTest>
#include <windows.h>
#include <QTemporaryDir>
#include "sensors/GpuFanState.h"
#include "sensors/NvmlFan.h"

using namespace NZXTKrakenPump;

class TestNvmlFan : public QObject {
    Q_OBJECT
    HMODULE m_mod = nullptr;
    QTemporaryDir m_dir;
    template <class F> F fn(const char* n) { return reinterpret_cast<F>(GetProcAddress(m_mod, n)); }
    void temp(unsigned t) { fn<void (*)(unsigned)>("fake_set_temp")(t); }
    unsigned pct(unsigned f) { return fn<unsigned (*)(unsigned)>("fake_pct")(f); }
    int manual() { return fn<int (*)()>("fake_manual")(); }

private slots:
    void initTestCase()
    {
        m_mod = LoadLibraryA(NVML_TEST_DLL_PATH);
        QVERIFY(m_mod);
        QVERIFY(m_dir.isValid());
        setGpuFanStateDir(m_dir.path());
    }
    void init() { temp(40); QFile::remove(gpuFanStatePath(QStringLiteral("nvml_recovery"))); }

    void floorLevelHasHysteresis()
    {
        QCOMPARE(nvmlFloorLevel(70, 0), 0);
        QCOMPARE(nvmlFloorLevel(75, 0), 60);
        QCOMPARE(nvmlFloorLevel(73, 60), 60);
        QCOMPARE(nvmlFloorLevel(71, 60), 0);
        QCOMPARE(nvmlFloorLevel(85, 0), 100);
        QCOMPARE(nvmlFloorLevel(83, 100), 100);
        QCOMPARE(nvmlFloorLevel(80, 100), 60);
        QCOMPARE(nvmlFloorLevel(60, 100), 0);
    }
    void guardActsWithoutNewDutyRequest()
    {
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 40));
        QCOMPARE(pct(0), 40u);
        temp(90);
        f.value(QStringLiteral("nvml/0/fan"));
        QCOMPARE(pct(0), 100u); QCOMPARE(pct(1), 100u);
        temp(78);
        f.value(QStringLiteral("nvml/0/fan"));
        QCOMPARE(pct(0), 60u);
        temp(60);
        f.value(QStringLiteral("nvml/0/fan"));
        QCOMPARE(pct(0), 40u);
        f.setDefault(QStringLiteral("nvml/0/control"));
    }
    void markerExistsWhileManualAndIsRemovedOnDefault()
    {
        const QString marker = gpuFanStatePath(QStringLiteral("nvml_recovery"));
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(!QFile::exists(marker));
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 50));
        QVERIFY(QFile::exists(marker));
        f.setDefault(QStringLiteral("nvml/0/control"));
        QVERIFY(!QFile::exists(marker));
    }
    void recoversAutomaticPolicyAfterUncleanExit()
    {
        auto* crashed = new NvmlFan;
        QVERIFY(crashed->init());
        QVERIFY(crashed->setDuty(QStringLiteral("nvml/0/control"), 50));
        QCOMPARE(manual(), 1);

        NvmlFan next;
        QVERIFY(next.init());
        QCOMPARE(manual(), 0);
        QVERIFY(!QFile::exists(gpuFanStatePath(QStringLiteral("nvml_recovery"))));
        delete crashed;
    }

    void enumeratesFakeGpu()
    {
        NvmlFan f;
        QVERIFY(f.init());
        QCOMPARE(f.channels().size(), 1);
        QCOMPARE(f.channels()[0].id, QStringLiteral("nvml/0/control"));
        QCOMPARE(f.channels()[0].rpmId, QStringLiteral("nvml/0/fan"));
        QCOMPARE(f.channels()[0].hw, QStringLiteral("Fake RTX 9999"));
        QVERIFY(NvmlFan::owns(f.channels()[0].id));
        QVERIFY(!NvmlFan::owns(QStringLiteral("adlx/0/control")));
    }
    void writesEveryFanAndReportsRpm()
    {
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 55));
        QCOMPARE(pct(0), 55u); QCOMPARE(pct(1), 55u);
        QCOMPARE(f.value(QStringLiteral("nvml/0/control")), 55.f);
        QCOMPARE(f.value(QStringLiteral("nvml/0/fan")), 1100.f);
        f.setDefault(QStringLiteral("nvml/0/control"));
    }
    void clampsBelowHardwareMinimum()
    {
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 10));   // min 30 %
        QCOMPARE(pct(0), 30u);
        f.setDefault(QStringLiteral("nvml/0/control"));
    }
    void thermalFloorOverridesLowCurve()
    {
        NvmlFan f; QVERIFY(f.init());
        temp(80);
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 40));
        QCOMPARE(pct(0), 60u);
        temp(90);
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 40));
        QCOMPARE(pct(0), 100u);                                     // >= 85 °C : 100 %
        temp(60);
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 40));
        QCOMPARE(pct(0), 40u);
        f.setDefault(QStringLiteral("nvml/0/control"));
    }
    void setDefaultRestoresAutomaticPolicy()
    {
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 50));
        QCOMPARE(manual(), 1);
        f.setDefault(QStringLiteral("nvml/0/control"));
        QCOMPARE(manual(), 0);
    }
    void shutdownRestoresAutomaticPolicy()
    {
        {
            NvmlFan f; QVERIFY(f.init());
            QVERIFY(f.setDuty(QStringLiteral("nvml/0/control"), 50));
            QCOMPARE(manual(), 1);
        }
        QCOMPARE(manual(), 0);
    }
    void untouchedGpuIsNeverWritten()
    {
        NvmlFan f; QVERIFY(f.init());
        f.setDefault(QStringLiteral("nvml/0/control"));
        QCOMPARE(manual(), 0);
    }
    void unknownIdIsRejected()
    {
        NvmlFan f; QVERIFY(f.init());
        QVERIFY(!f.setDuty(QStringLiteral("nvml/7/control"), 50));
        QVERIFY(!f.setDuty(QStringLiteral("garbage"), 50));
    }
};

QTEST_APPLESS_MAIN(TestNvmlFan)
#include "tst_nvmlfan.moc"
