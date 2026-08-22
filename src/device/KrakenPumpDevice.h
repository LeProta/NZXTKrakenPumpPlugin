#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <QString>
#include <QMutex>

// hidapi forward
struct hid_device_;
typedef hid_device_ hid_device;

#ifdef _WIN32
#  include <windows.h>
#endif

namespace NZXTKrakenPump {

// ─── Périphérique connu ──────────────────────────────────────────────────────
// Pas de canal LCD ici : uniquement l'interface HID (commandes + capteurs).
// Coexiste avec le plugin KrakenLCD : Windows duplique chaque input report
// vers chaque handle ouvert et sérialise les WriteFile par rapport complet.
struct PumpDeviceInfo {
    uint16_t    vid;
    uint16_t    pid;
    const char* name;
    int         hidReportLen;   // 64 ou 512
    bool        hasFanChannel;  // ventilateurs branchés sur la pompe (Z3/Elite)
    // Protocole 0x72 (cf. LibreHardwareMonitor KrakenV3.cs) :
    //   ancien (X3/Z3)          : 0x72, cid, 0x00, 0x00 + 40 duties (20..59 °C)
    //   nouveau (Elite/2023/V2) : 0x72, 0x01, 0x01, 0x00 (pompe)
    //                             0x72, 0x02, 0x01, 0x01 (fans) + 60 duties (0..59 °C)
    bool        newProto;
};

inline const std::vector<PumpDeviceInfo> SUPPORTED_DEVICES = {
    //                                                    HID   fans  proto
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

// ─── Lectures capteurs ───────────────────────────────────────────────────────
struct PumpReadings {
    float liquidTemp = 0.f; // °C
    int   pumpRPM    = 0;
    int   pumpDuty   = 0;   // % rapporté par le device
    int   fanRPM     = 0;
    int   fanDuty    = 0;
};

// Canaux du protocole 0x72 (identiques à liquidctl)
enum class Channel : uint8_t {
    Pump = 0x01,
    Fan  = 0x02,
};

// ─── Classe de communication ─────────────────────────────────────────────────
class KrakenPumpDevice {
public:
    KrakenPumpDevice();
    ~KrakenPumpDevice();

    bool open();                              // premier Kraken compatible
    bool open(uint16_t vid, uint16_t pid);
    void close();
    bool isOpen() const;

    const PumpDeviceInfo* info() const { return m_info; }
    QString displayName() const;

    // Séquence d'init (intervalle de statut + requête firmware) — best effort
    bool initialize();

    // Courbe température liquide -> duty : 60 valeurs pour 0..59 °C.
    // Ancien protocole : seules les entrées 20..59 sont envoyées (40 valeurs).
    bool setCurve(Channel ch, const std::array<uint8_t, 60>& duty);

    // Statut 0x74 0x01 -> 0x75 0x01 (temp liquide, RPM, duty).
    // Ignore les rapports étrangers (ex. acks LCD 0x37 du plugin KrakenLCD).
    bool readStatus(PumpReadings& out);

    // Sondage léger (hid_enumerate) pour la reconnexion à chaud.
    static bool anySupportedPresent();

private:
    bool openHID(uint16_t vid, uint16_t pid);
    bool sendHIDCommand(const uint8_t* data, size_t len);
    bool readHIDResponse(uint8_t* buf, size_t maxLen, int timeoutMs = 300);
    int  hidReportLen() const;

#ifdef _WIN32
    // WinHID direct — contourne le bug WriteFile d'hidapi 0.14.
    // Handle OVERLAPPED : lecture par ReadFile = file de rapports du driver
    // (sémantique "prochain rapport" + timeout réel).
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
    mutable QMutex        m_cmdMutex;   // sérialise commandes + lecture statut
};

} // namespace NZXTKrakenPump
