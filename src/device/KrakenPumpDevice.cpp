#include "KrakenPumpDevice.h"
#include <hidapi/hidapi.h>
#include <cstring>
#include <algorithm>
#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <setupapi.h>
#  include <hidsdi.h>
#  pragma comment(lib, "hid.lib")
#  pragma comment(lib, "setupapi.lib")
#endif

namespace NZXTKrakenPump {

// ────────────────────────────────────────────────────────────────────────────
KrakenPumpDevice::KrakenPumpDevice()
{
    hid_init();
}

KrakenPumpDevice::~KrakenPumpDevice()
{
    close();
    // PAS de hid_exit() : état hidapi global au process (cf. plugin KrakenLCD).
}

int KrakenPumpDevice::hidReportLen() const
{
    return m_info ? m_info->hidReportLen : 64;
}

// ── Détection / ouverture ────────────────────────────────────────────────────
bool KrakenPumpDevice::open()
{
    for (const auto& dev : SUPPORTED_DEVICES) {
        if (open(dev.vid, dev.pid)) return true;
    }
    return false;
}

bool KrakenPumpDevice::open(uint16_t vid, uint16_t pid)
{
    close();
    for (const auto& dev : SUPPORTED_DEVICES) {
        if (dev.vid == vid && dev.pid == pid) {
            m_info = &dev;
            break;
        }
    }
    if (!m_info) return false;

    openHID(vid, pid);
#ifdef _WIN32
    openWinHID(vid, pid);
#endif

    bool hasCmd = (m_hid != nullptr);
#ifdef _WIN32
    hasCmd = hasCmd || (m_winHidHandle != INVALID_HANDLE_VALUE);
#endif
    if (!hasCmd) {
        close();
        return false;
    }

    qInfo() << "[KrakenPump/Device] opened:" << displayName()
            << "— commands:" << (m_hid ? "HID" : "WinHID")
            << " hidReportLen:" << hidReportLen()
            << " fanChannel:" << m_info->hasFanChannel;
    return true;
}

void KrakenPumpDevice::close()
{
    if (m_hid) { hid_close(m_hid); m_hid = nullptr; }
#ifdef _WIN32
    closeWinHID();
#endif
    m_info = nullptr;
}

bool KrakenPumpDevice::isOpen() const
{
    if (m_hid) return true;
#ifdef _WIN32
    if (m_winHidHandle != INVALID_HANDLE_VALUE) return true;
#endif
    return false;
}

QString KrakenPumpDevice::displayName() const
{
    return m_info ? QString::fromUtf8(m_info->name) : QStringLiteral("Not connected");
}

// ── HID classique (hidapi) ───────────────────────────────────────────────────
bool KrakenPumpDevice::openHID(uint16_t vid, uint16_t pid)
{
    m_hid = hid_open(vid, pid, nullptr);
    if (!m_hid) return false;
    hid_set_nonblocking(m_hid, 1);
    qDebug() << "[KrakenPump/HID] hid_open OK for"
             << QString::asprintf("VID_%04X&PID_%04X", vid, pid);
    return true;
}

// Le paquet envoyé doit faire exactement reportLen(+1 ReportID) octets —
// 64 pour X3/Z3/Elite, 512 pour Elite V2.
bool KrakenPumpDevice::sendHIDCommand(const uint8_t* data, size_t len)
{
    if (!data) return false;

    const int reportLen = hidReportLen();

#ifdef _WIN32
    // Plan A : WinHID direct (WriteFile overlapped) — voie fiable sous Windows,
    // contourne le bug d'hidapi 0.14 sur WriteFile.
    if (m_winHidHandle != INVALID_HANDLE_VALUE) {
        if (winHidWrite(data, len))
            return true;
    }
#endif

    // Plan B : hidapi
    if (m_hid) {
        const int pktSize = reportLen + 1; // +1 ReportID
        std::vector<uint8_t> pkt(pktSize, 0);
        pkt[0] = 0x00;
        if (len > (size_t)reportLen) len = reportLen;
        std::memcpy(pkt.data() + 1, data, len);

        int res = hid_write(m_hid, pkt.data(), pktSize);
        if (res == pktSize) return true;

        const wchar_t* err = hid_error(m_hid);
        static int s_hidErr = 0;
        if (s_hidErr++ < 3) {
            qWarning() << "[KrakenPump/HID] hid_write returned" << res
                       << "— cause:" << (err ? QString::fromWCharArray(err) : QStringLiteral("(null)"))
                       << "— cmd[0]=" << QString::asprintf("0x%02X", (unsigned)data[0]);
        }
    }

    return false;
}

bool KrakenPumpDevice::readHIDResponse(uint8_t* buf, size_t maxLen, int timeoutMs)
{
    if (m_hid) {
        int r = hid_read_timeout(m_hid, buf, maxLen, timeoutMs);
        return r > 0;
    }
#ifdef _WIN32
    if (m_winHidHandle != INVALID_HANDLE_VALUE) {
        int bytesRead = 0;
        if (winHidRead(buf, maxLen, bytesRead, timeoutMs))
            return bytesRead > 0;
    }
#endif
    return false;
}

// ── Init ─────────────────────────────────────────────────────────────────────
bool KrakenPumpDevice::initialize()
{
    QMutexLocker lock(&m_cmdMutex);

    // Purge des rapports en attente (bornée)
    uint8_t discard[512];
    for (int i = 0; i < 16 && readHIDResponse(discard, sizeof(discard), 1); ++i) {}

    // Intervalle des rapports de statut + requête firmware (cf. liquidctl X3/Z3)
    const uint8_t interval[5] = { 0x70, 0x02, 0x01, 0xB8, 0x01 };
    const uint8_t fwReq[2]    = { 0x70, 0x01 };
    bool ok = sendHIDCommand(interval, sizeof(interval));
    ok = sendHIDCommand(fwReq, sizeof(fwReq)) && ok;

    // Drainer les réponses 0x71 (non exploitées, best effort)
    for (int i = 0; i < 4 && readHIDResponse(discard, sizeof(discard), 50); ++i) {}
    return ok;
}

// ── Courbe de vitesse ────────────────────────────────────────────────────────
bool KrakenPumpDevice::setCurve(Channel ch, const std::array<uint8_t, 60>& duty)
{
    if (!isOpen() || !m_info) return false;
    QMutexLocker lock(&m_cmdMutex);

    uint8_t cmd[64] = { 0 };
    size_t  len     = 0;
    if (m_info->newProto) {
        // Elite/2023/Elite V2 : en-tête spécifique par canal + 60 duties (0..59 °C)
        cmd[0] = 0x72;
        cmd[1] = static_cast<uint8_t>(ch);
        cmd[2] = 0x01;
        cmd[3] = (ch == Channel::Fan) ? 0x01 : 0x00;
        for (size_t i = 0; i < 60; ++i)
            cmd[4 + i] = std::min<uint8_t>(duty[i], 100);
        len = 64;
    } else {
        // X3/Z3 : 0x72, canal, 0x00, 0x00 + 40 duties (20..59 °C)
        cmd[0] = 0x72;
        cmd[1] = static_cast<uint8_t>(ch);
        for (size_t i = 0; i < 40; ++i)
            cmd[4 + i] = std::min<uint8_t>(duty[20 + i], 100);
        len = 44;
    }

    if (!sendHIDCommand(cmd, len)) {
        qWarning() << "[KrakenPump/Device] setCurve failed, channel ="
                   << int(ch);
        return false;
    }
    return true;
}

// ── Statut ───────────────────────────────────────────────────────────────────
bool KrakenPumpDevice::readStatus(PumpReadings& out)
{
    if (!isOpen()) return false;
    QMutexLocker lock(&m_cmdMutex);

    const uint8_t req[2] = { 0x74, 0x01 };
    if (!sendHIDCommand(req, sizeof(req)))
        return false;

    const int maxBuf = hidReportLen() + 1;
    std::vector<uint8_t> buf(maxBuf, 0);

    // Boucle bornée : la file peut contenir des rapports étrangers (acks 0x37
    // du plugin KrakenLCD, réponses 0x71...) — on les saute jusqu'au 0x75 0x01.
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 500) {
        std::fill(buf.begin(), buf.end(), 0);
        if (!readHIDResponse(buf.data(), buf.size(), 100))
            continue;

        const uint8_t* p = buf.data();
        if (p[0] == 0x00 && p[1] == 0x75 && p[2] == 0x01)
            p++; // skip ReportID
        if (p[0] != 0x75 || p[1] != 0x01)
            continue;

        // Offsets identiques à liquidctl / plugin SignalRGB
        out.liquidTemp = static_cast<float>(p[15]) + static_cast<float>(p[16]) / 10.0f;
        out.pumpRPM    = (static_cast<int>(p[18]) << 8) | p[17];
        out.pumpDuty   = p[19];
        out.fanRPM     = (static_cast<int>(p[24]) << 8) | p[23];
        out.fanDuty    = p[25];
        return true;
    }
    return false;
}

// ── Sondage de présence ──────────────────────────────────────────────────────
bool KrakenPumpDevice::anySupportedPresent()
{
    hid_device_info* list = hid_enumerate(0x1E71, 0);
    bool found = false;
    for (hid_device_info* p = list; p && !found; p = p->next)
        for (const auto& dev : SUPPORTED_DEVICES)
            if (p->product_id == dev.pid) { found = true; break; }
    hid_free_enumeration(list);
    return found;
}

// =============================================================================
// Windows : WinHID direct — contourne le bug d'hidapi 0.14.0 sur WriteFile
// =============================================================================
#ifdef _WIN32

bool KrakenPumpDevice::openWinHID(uint16_t vid, uint16_t pid)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);
        std::vector<BYTE> detailBuf(reqSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, nullptr))
            continue;

        std::wstring path(detail->DevicePath);
        wchar_t vidStr[16], pidStr[16];
        swprintf(vidStr, 16, L"vid_%04x", vid);
        swprintf(pidStr, 16, L"pid_%04x", pid);
        if (path.find(vidStr) == std::wstring::npos || path.find(pidStr) == std::wstring::npos)
            continue;

        // FILE_FLAG_OVERLAPPED : requis pour ReadFile avec timeout (winHidRead)
        HANDLE h = CreateFileW(detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(attrs);
        if (!HidD_GetAttributes(h, &attrs) || attrs.VendorID != vid || attrs.ProductID != pid) {
            CloseHandle(h);
            continue;
        }

        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps;
        if (!HidD_GetPreparsedData(h, &pp)) { CloseHandle(h); continue; }
        if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) {
            HidD_FreePreparsedData(pp); CloseHandle(h); continue;
        }
        HidD_FreePreparsedData(pp);

        m_winHidOutputLen = caps.OutputReportByteLength;
        m_winHidInputLen  = caps.InputReportByteLength;
        m_winHidHandle    = h;
        m_winHidReadEvt   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_winHidWriteEvt  = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        qDebug() << "[KrakenPump/HID] HID device opened:"
                 << "OutputReportByteLength=" << m_winHidOutputLen
                 << "InputReportByteLength=" << m_winHidInputLen;

        SetupDiDestroyDeviceInfoList(devInfo);
        return true;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

void KrakenPumpDevice::closeWinHID()
{
    if (m_winHidHandle != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_winHidHandle, nullptr);
        CloseHandle(m_winHidHandle);
        m_winHidHandle = INVALID_HANDLE_VALUE;
    }
    if (m_winHidReadEvt)  { CloseHandle(m_winHidReadEvt);  m_winHidReadEvt  = nullptr; }
    if (m_winHidWriteEvt) { CloseHandle(m_winHidWriteEvt); m_winHidWriteEvt = nullptr; }
    m_winHidOutputLen = 0;
    m_winHidInputLen  = 0;
}

bool KrakenPumpDevice::winHidWrite(const uint8_t* data, size_t len)
{
    if (m_winHidHandle == INVALID_HANDLE_VALUE || !data || m_winHidOutputLen <= 0)
        return false;

    // === Stratégie 1 : WriteFile direct (pas de ReportID préfixé — rapports
    // Vendor Defined sans ReportID numéroté). Handle OVERLAPPED. ===
    {
        std::vector<BYTE> buf(m_winHidOutputLen, 0);
        const size_t toCopy = std::min(len, buf.size());
        std::memcpy(buf.data(), data, toCopy);

        OVERLAPPED ov{};
        ov.hEvent = m_winHidWriteEvt;
        ResetEvent(m_winHidWriteEvt);
        DWORD written = 0;
        BOOL ok = WriteFile(m_winHidHandle, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(m_winHidWriteEvt, 1000) == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(m_winHidHandle, &ov, &written, FALSE);
            } else {
                CancelIoEx(m_winHidHandle, &ov);
                GetOverlappedResult(m_winHidHandle, &ov, &written, TRUE);
                ok = FALSE;
            }
        } else if (ok) {
            GetOverlappedResult(m_winHidHandle, &ov, &written, TRUE);
        }
        if (ok) return true;

        DWORD err1 = GetLastError();
        static int s_wfErr = 0;
        if (s_wfErr++ < 3)
            qWarning() << "[KrakenPump/HID] WriteFile failed, GetLastError=" << err1
                       << "size=" << m_winHidOutputLen;
    }

    // === Stratégie 2 : HidD_SetOutputReport avec ReportID=0 préfixé ===
    {
        std::vector<BYTE> buf(m_winHidOutputLen, 0);
        const size_t toCopy = std::min(len, buf.size() - 1);
        std::memcpy(buf.data() + 1, data, toCopy);
        if (HidD_SetOutputReport(m_winHidHandle, buf.data(), static_cast<ULONG>(buf.size())))
            return true;
    }

    // === Stratégie 3 : HidD_SetOutputReport avec data[0] comme ReportID ===
    {
        std::vector<BYTE> buf(m_winHidOutputLen, 0);
        if (len <= buf.size()) {
            std::memcpy(buf.data(), data, len);
            if (HidD_SetOutputReport(m_winHidHandle, buf.data(), static_cast<ULONG>(buf.size())))
                return true;
        }
    }

    const DWORD err = GetLastError();
    static int s_errCount = 0;
    if (s_errCount++ < 3) {
        qWarning() << "[KrakenPump/HID] All strategies failed, last GetLastError=" << err
                   << "cmd[0]=" << QString::asprintf("0x%02X", (unsigned)data[0]);
    }
    return false;
}

bool KrakenPumpDevice::winHidRead(uint8_t* data, size_t maxLen, int& bytesRead, int timeoutMs)
{
    bytesRead = 0;
    if (m_winHidHandle == INVALID_HANDLE_VALUE || !data || m_winHidInputLen <= 0)
        return false;

    std::vector<BYTE> buf(m_winHidInputLen, 0);

    OVERLAPPED ov{};
    ov.hEvent = m_winHidReadEvt;
    ResetEvent(m_winHidReadEvt);
    DWORD read = 0;
    BOOL ok = ReadFile(m_winHidHandle, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov);
    if (!ok) {
        if (GetLastError() != ERROR_IO_PENDING)
            return false;
        if (WaitForSingleObject(m_winHidReadEvt, timeoutMs > 0 ? DWORD(timeoutMs) : 0) != WAIT_OBJECT_0) {
            CancelIoEx(m_winHidHandle, &ov);
            GetOverlappedResult(m_winHidHandle, &ov, &read, TRUE);
            return false;
        }
    }
    if (!GetOverlappedResult(m_winHidHandle, &ov, &read, TRUE) || read == 0)
        return false;

    // Skip le ReportID (byte 0)
    const size_t toCopy = std::min<size_t>(read > 0 ? size_t(read) - 1 : 0, maxLen);
    std::memcpy(data, buf.data() + 1, toCopy);
    bytesRead = static_cast<int>(toCopy);
    return true;
}

#endif // _WIN32

} // namespace NZXTKrakenPump
