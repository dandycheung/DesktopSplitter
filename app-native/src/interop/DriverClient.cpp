#define INITGUID
#include "interop/DriverClient.h"
#include <winioctl.h>

namespace ds {
namespace {

wstring Win32Message(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    wstring s = buf ? buf : (L"Win32 " + std::to_wstring(err));
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
    return s;
}

HANDLE OpenDevice() {
    wstring path = DriverClient::GetDevicePath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        throw DriverIoException(Narrow(
            L"Could not open the driver device '" + path + L"': " + Win32Message(err) +
            L" (Win32 " + std::to_wstring(err) + L")."));
    }
    return h;
}

} // namespace

DESKSPLIT_CONFIG DriverClient::NewConfig() {
    DESKSPLIT_CONFIG cfg{};
    cfg.Version = DESKSPLIT_PROTOCOL_VERSION;
    cfg.MonitorCount = 0;
    return cfg;
}

bool DriverClient::TryFindDevicePath(wstring& devicePath) {
    devicePath.clear();
    GUID guid = GUID_DEVINTERFACE_DESKSPLIT;
    wchar_t empty = 0;
    ULONG len = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &len, &guid, &empty, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    // Some SDKs want pDeviceID as PWSTR NULL. Use nullptr.
    cr = CM_Get_Device_Interface_List_SizeW(
        &len, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS || len < 2) return false;

    std::vector<wchar_t> buffer(len);
    cr = CM_Get_Device_Interface_ListW(
        &guid, nullptr, buffer.data(), len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) return false;

    const wchar_t* p = buffer.data();
    while (*p) {
        wstring s(p);
        if (!Trim(s).empty()) {
            devicePath = s;
            return true;
        }
        p += s.size() + 1;
    }
    return false;
}

wstring DriverClient::GetDevicePath() {
    wstring path;
    if (!TryFindDevicePath(path))
        throw DriverNotFoundException(Narrow(DriverMissingMessage));
    return path;
}

void DriverClient::SetConfig(const DESKSPLIT_CONFIG& config) {
    if (config.MonitorCount > DESKSPLIT_MAX_MONITORS) {
        throw DriverIoException("MonitorCount exceeds DESKSPLIT_MAX_MONITORS.");
    }
    HANDLE h = OpenDevice();
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_DESKSPLIT_SET_CONFIG,
                              (void*)&config, sizeof(config),
                              nullptr, 0, &returned, nullptr);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        throw DriverIoException(Narrow(
            L"IOCTL_DESKSPLIT_SET_CONFIG failed: " + Win32Message(err) +
            L" (Win32 " + std::to_wstring(err) + L")."));
    }
}

DESKSPLIT_STATUS DriverClient::GetStatus() {
    HANDLE h = OpenDevice();
    DESKSPLIT_STATUS status{};
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_DESKSPLIT_GET_STATUS,
                              nullptr, 0, &status, sizeof(status),
                              &returned, nullptr);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        throw DriverIoException(Narrow(
            L"IOCTL_DESKSPLIT_GET_STATUS failed: " + Win32Message(err) +
            L" (Win32 " + std::to_wstring(err) + L")."));
    }
    if (returned < sizeof(status)) {
        throw DriverIoException("IOCTL_DESKSPLIT_GET_STATUS returned fewer bytes than expected.");
    }
    return status;
}

void DriverClient::ClearConfig() {
    DESKSPLIT_CONFIG cfg = NewConfig();
    cfg.MonitorCount = 0;
    SetConfig(cfg);
}

} // namespace ds
