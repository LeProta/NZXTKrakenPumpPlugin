#pragma once
#include <QMap>
#include <QStringList>
#include <QVector>
#include <array>
#include <cstdint>
#include "device/KrakenPumpDevice.h"

namespace fake {

struct Device {
    bool present = true;           // anySupportedPresent()
    bool openOk  = true;
    bool isOpen  = false;
    bool readOk  = true;           // readStatus()
    NZXTKrakenPump::PumpReadings readings;
    int  pid     = 0x300E;
    QVector<std::array<uint8_t, 60>> pumpCurves, fanCurves;
};

struct Lhwm {
    struct Ch { QString id, rpmId, name, hw; };
    QVector<Ch>            channels;
    QStringList            tempIds, tempNames;
    QMap<QString, float>   values;
    int                    failSetDuty = 0;
    QVector<QPair<QString, int>> setDutyCalls;
    QStringList            setDefaultCalls;
};

extern Device dev;
extern Lhwm   lhwm;
void reset();

} // namespace fake
