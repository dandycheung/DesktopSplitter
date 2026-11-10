#include "interop/MonitorSpecialization.h"
#include "interop/DisplayConfig.h"

namespace ds {
namespace {

// GUID_MONITOR_OVERRIDE_PSEUDO_SPECIALIZED
const GUID kSpecializationType =
    { 0xF196C02F, 0xF86F, 0x4F9A, { 0xAA, 0x15, 0xE9, 0xCE, 0xBD, 0xFE, 0x3B, 0x96 } };
const GUID kSpecializationSubType = {};
const wchar_t kApplicationName[] = L"DesktopSplitter";

DISPLAYCONFIG_DEVICE_INFO_HEADER BuildHeader(const MonitorInfo& monitor, UINT32 type, UINT32 size) {
    DISPLAYCONFIG_DEVICE_INFO_HEADER h{};
    h.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)type;
    h.size = size;
    h.adapterId.LowPart = monitor.AdapterIdLow;
    h.adapterId.HighPart = monitor.AdapterIdHigh;
    h.id = monitor.TargetId;
    return h;
}

wstring GuidStr(const GUID& g) {
    wchar_t buf[64];
    swprintf_s(buf, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
               (unsigned long)g.Data1, g.Data2, g.Data3,
               g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
               g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

wstring DescribePrimary(const MonitorInfo& monitor) {
    try {
        auto all = DisplayConfig::Enumerate();
        const MonitorInfo* primary = nullptr;
        for (const auto& m : all) if (m.IsPrimary) { primary = &m; break; }
        if (!primary) return L"No display currently reports as primary.";
        if (EqualsIgnoreCase(primary->DeviceName, monitor.DeviceName))
            return L"This monitor IS currently the primary display (" + primary->DeviceName +
                   L"), and Windows only allows non-primary displays to be removed from the desktop.";
        return L"This monitor is not primary \u2014 " + primary->DisplayLabel() + L" (" +
               primary->DeviceName + L") is \u2014 so the primary-display restriction is not the cause.";
    } catch (const DisplayConfigException&) {
        return L"The current primary display could not be determined.";
    }
}

wstring Explain(int rc, const MonitorInfo& monitor, bool enabled,
                const DS_DISPLAYCONFIG_SET_MONITOR_SPECIALIZATION& packet) {
    wstring action = enabled ? L"remove from the desktop" : L"restore to the desktop";
    wstring what = L"Could not " + action + L" " + monitor.DisplayLabel() + L" (" + monitor.DeviceName + L").";
    wchar_t params[512];
    swprintf_s(params,
        L"Request sent: adapter=%08X-%08X target=%u, size=%u, value=0x%X, type={%s}, subType={%s}, app='%s'.",
        (unsigned)monitor.AdapterIdHigh, monitor.AdapterIdLow, monitor.TargetId,
        packet.header.size, packet.value,
        GuidStr(packet.specializationType).c_str(),
        GuidStr(packet.specializationSubType).c_str(),
        packet.specializationApplicationName);
    wstring primaryNote = DescribePrimary(monitor);
    switch (rc) {
    case 5:
        return what + L" Windows denied access (ERROR_ACCESS_DENIED). Measured on this API: an "
               L"unelevated process is refused for every monitor and every parameter combination, "
               L"so this almost certainly needs administrator rights. DesktopSplitter will not "
               L"elevate itself \u2014 use the Windows Settings route instead.\r\n" + params;
    case 87:
        return what + L" Windows rejected the parameters (ERROR_INVALID_PARAMETER). " +
               primaryNote + L"\r\n" + params;
    case 50:
        return what + L" Not supported on this system (ERROR_NOT_SUPPORTED).\r\n" + params;
    case 31:
        return what + L" Windows reported a general failure (ERROR_GEN_FAILURE).\r\n" + params;
    default: {
        wchar_t* buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, rc, 0, (LPWSTR)&buf, 0, nullptr);
        wstring msg = buf ? buf : L"";
        if (buf) LocalFree(buf);
        return what + L" " + Trim(msg) + L" (error " + std::to_wstring(rc) + L").\r\n" + params;
    }
    }
}

} // namespace

bool MonitorSpecialization::TryGet(const MonitorInfo& monitor, SpecializationState& state, int& errorCode) {
    DS_DISPLAYCONFIG_GET_MONITOR_SPECIALIZATION packet{};
    packet.header = BuildHeader(monitor, DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION, sizeof(packet));
    errorCode = DisplayConfigGetDeviceInfo(&packet.header);
    if (errorCode != ERROR_SUCCESS) {
        state = {};
        return false;
    }
    state.RawValue = packet.value;
    return true;
}

std::optional<SpecializationState> MonitorSpecialization::Get(const MonitorInfo& monitor) {
    SpecializationState s; int rc = 0;
    if (TryGet(monitor, s, rc)) return s;
    return std::nullopt;
}

SpecializationResult MonitorSpecialization::Set(const MonitorInfo& monitor, bool enabled) {
    DS_DISPLAYCONFIG_SET_MONITOR_SPECIALIZATION packet{};
    packet.header = BuildHeader(monitor, DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION, sizeof(packet));
    packet.value = enabled ? 1u : 0u;
    packet.specializationType = kSpecializationType;
    packet.specializationSubType = kSpecializationSubType;
    wcsncpy_s(packet.specializationApplicationName, kApplicationName, _TRUNCATE);
    int rc = DisplayConfigSetDeviceInfo(&packet.header);
    if (rc == ERROR_SUCCESS) {
        return { true, rc, monitor.DisplayLabel() + L": specialization " +
                           (enabled ? L"ENABLED" : L"disabled") + L"." };
    }
    return { false, rc, Explain(rc, monitor, enabled, packet) };
}

} // namespace ds
