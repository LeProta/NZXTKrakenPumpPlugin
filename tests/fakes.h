#pragma once
// État des doublures (fakes) de KrakenPumpDevice et LhwmFans, liées à la place
// des vraies implémentations : PumpWorker est testé sans HID ni LibreHardwareMonitor.
#include <QMap>
#include <QStringList>
#include <QVector>
#include <array>
#include <cstdint>
#include "device/KrakenPumpDevice.h"

namespace fake {

struct Device {
    bool present = true;           // anySupportedPresent()
    bool openOk  = true;           // open()
    bool isOpen  = false;
    bool readOk  = true;           // readStatus()
    NZXTKrakenPump::PumpReadings readings;
    int  pid     = 0x300E;         // Kraken 2023 : canal fan + nouveau protocole
    QVector<std::array<uint8_t, 60>> pumpCurves, fanCurves;   // setCurve() reçus
};

struct Lhwm {
    struct Ch { QString id, rpmId, name, hw; };
    QVector<Ch>            channels;
    QStringList            tempIds, tempNames;
    QMap<QString, float>   values;          // value(id)
    int                    failSetDuty = 0; // nb d'échecs à simuler pour setDuty
    QVector<QPair<QString, int>> setDutyCalls;
    QStringList            setDefaultCalls;
};

extern Device dev;
extern Lhwm   lhwm;
void reset();

} // namespace fake
