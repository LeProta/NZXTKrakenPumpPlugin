#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <QString>
#include <QMutex>

struct hid_device_;
typedef hid_device_ hid_device;

#ifdef _WIN32
#  include <windows.h>
#endif

namespace NZXTKrakenPump {

struct PumpDeviceInfo {
    uint16_t    vid;
    uint16_t    pid;
    const char* name;
    int         hidReportLen;
    bool        hasFanChannel;
    bool        newProto;
};

inline const std::vector<PumpDeviceInfo> SUPPORTED_DEVICES = {
    { 0x1E71, 0x2007, "NZXT Kraken X53/X63/X73",           64, false, false },
    { 0x1E71, 0x2014, "NZXT Kraken X53/X63/X73 RGB",       64, false, false },
    { 0x1E71, 0x3008, "NZXT Kraken Z73",                   64, true,  false },
    { 0x1E71, 0x3009, "NZXT Kraken Z53",                   64, true,  false },
    { 0x1E71, 0x300A, "NZXT Kraken Z63",                   64, true,  false },
    { 0x1E71, 0x300C, "NZXT Kraken Elite 360",             64, true,  true  },
    { 0x1E71, 0x300E, "NZXT Kraken 2023",                  64, true,  true  },
    { 0x1E71, 0x3012, "NZXT Kraken Elite V2",             512, true,  true  },
    { 0x1E71, 0x3013, "NZXT Kraken Elite V2 B",           512, true,  true  },
};

struct PumpReadings {
    float liquidTemp = 0.f; // °C
    int   pumpRPM    = 0;
    int   pumpDuty   = 0;
    int   fanRPM     = 0;
    int   fanDuty    = 0;
};

enum class Channel : uint8_t {
    Pump = 0x01,
    Fan  = 0x02,
};

class KrakenPumpDevice {
public:
    KrakenPumpDevice();
    ~KrakenPumpDevice();

    bool open();
    bool open(uint16_t vid, uint16_t pid);
    void close();
    bool isOpen() const;

    const PumpDeviceInfo* info() const { return m_info; }
    QString displayName() const;

    bool initialize();

    bool setCurve(Channel ch, const std::array<uint8_t, 60>& duty);

    bool readStatus(PumpReadings& out);

    static bool anySupportedPresent();

private:
    bool openHID(uint16_t vid, uint16_t pid);
    bool sendHIDCommand(const uint8_t* data, size_t len);
    bool readHIDResponse(uint8_t* buf, size_t maxLen, int timeoutMs = 300);
    int  hidReportLen() const;

#ifdef _WIN32
    bool openWinHID(uint16_t vid, uint16_t pid);
    void closeWinHID();
    bool winHidWrite(const uint8_t* data, size_t len);
    bool winHidRead(uint8_t* data, size_t maxLen, int& bytesRead, int timeoutMs);
    HANDLE m_winHidHandle    = INVALID_HANDLE_VALUE;
    HANDLE m_winHidReadEvt   = nullptr;
    HANDLE m_winHidWriteEvt  = nullptr;
    int    m_winHidOutputLen = 0;
    int    m_winHidInputLen  = 0;
#endif

    hid_device*           m_hid  = nullptr;
    const PumpDeviceInfo* m_info = nullptr;
    mutable QMutex        m_cmdMutex;
};

}
