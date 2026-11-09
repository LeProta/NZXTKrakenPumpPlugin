#include "fakes.h"
#include "sensors/LhwmFans.h"

namespace fake {
Device dev;
Lhwm   lhwm;
void reset() { dev = Device(); lhwm = Lhwm(); }
} // namespace fake

namespace NZXTKrakenPump {

KrakenPumpDevice::KrakenPumpDevice() {}
KrakenPumpDevice::~KrakenPumpDevice() {}

bool KrakenPumpDevice::open()
{
    if (!fake::dev.present || !fake::dev.openOk) return false;
    for (const auto& d : SUPPORTED_DEVICES)
        if (d.pid == fake::dev.pid) m_info = &d;
    fake::dev.isOpen = (m_info != nullptr);
    return fake::dev.isOpen;
}
void KrakenPumpDevice::close() { fake::dev.isOpen = false; m_info = nullptr; }
bool KrakenPumpDevice::isOpen() const { return fake::dev.isOpen; }
QString KrakenPumpDevice::displayName() const
{
    return m_info ? QString::fromUtf8(m_info->name) : QStringLiteral("Not connected");
}
bool KrakenPumpDevice::initialize() { return true; }

bool KrakenPumpDevice::setCurve(Channel ch, const std::array<uint8_t, 60>& duty)
{
    if (!fake::dev.isOpen) return false;
    (ch == Channel::Pump ? fake::dev.pumpCurves : fake::dev.fanCurves).append(duty);
    return true;
}

bool KrakenPumpDevice::readStatus(PumpReadings& out)
{
    if (!fake::dev.isOpen || !fake::dev.readOk) return false;
    out = fake::dev.readings;
    return true;
}

bool KrakenPumpDevice::anySupportedPresent() { return fake::dev.present; }

bool LhwmFans::init()
{
    m_channels.clear();
    m_temps.clear();
    for (const auto& c : fake::lhwm.channels)
        m_channels.append({ c.id, c.rpmId, c.name, c.hw });
    for (int i = 0; i < fake::lhwm.tempIds.size(); ++i)
        m_temps.append({ fake::lhwm.tempIds[i], fake::lhwm.tempNames.value(i) });
    m_ready = true;
    m_lhwmChannels = m_channels.size();
    return true;
}

float LhwmFans::value(const QString& id) const { return fake::lhwm.values.value(id, 0.f); }

bool LhwmFans::setDuty(const QString& id, int percent)
{
    fake::lhwm.setDutyCalls.append({ id, percent });
    if (fake::lhwm.failSetDuty > 0) { --fake::lhwm.failSetDuty; return false; }
    fake::lhwm.values[id] = float(percent);
    return true;
}

void LhwmFans::setDefault(const QString& id) { fake::lhwm.setDefaultCalls.append(id); }

struct AdlxFan::Impl {};
AdlxFan::AdlxFan() : m_d(new Impl) {}
AdlxFan::~AdlxFan() {}
bool  AdlxFan::init() { return false; }
void  AdlxFan::shutdown() {}
float AdlxFan::value(const QString&) const { return 0.f; }
bool  AdlxFan::setDuty(const QString&, int) { return false; }
void  AdlxFan::setDefault(const QString&) {}

struct NvmlFan::Impl {};
NvmlFan::NvmlFan() : m_d(new Impl) {}
NvmlFan::~NvmlFan() {}
bool  NvmlFan::init() { return false; }
void  NvmlFan::shutdown() {}
float NvmlFan::value(const QString&) const { return 0.f; }
bool  NvmlFan::setDuty(const QString&, int) { return false; }
void  NvmlFan::setDefault(const QString&) {}

}
