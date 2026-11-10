#include "interop/DisplayConfig.h"
#include "interop/DriverClient.h"

namespace ds {
namespace {

const wchar_t* kAdapterMarkers[] = { L"desktopsplitter", L"desktopsplittervdd", L"desksplit" };

uint32_t ToMillihertz(const DISPLAYCONFIG_RATIONAL& r) {
    if (r.Denominator == 0 || r.Numerator == 0) return 0;
    return (uint32_t)std::lround(r.Numerator * 1000.0 / r.Denominator);
}

wstring DecodeVendor(uint16_t value) {
    int c1 = ((value >> 10) & 0x1F) + 'A' - 1;
    int c2 = ((value >> 5) & 0x1F) + 'A' - 1;
    int c3 = (value & 0x1F) + 'A' - 1;
    if (c1 < 'A' || c1 > 'Z' || c2 < 'A' || c2 > 'Z' || c3 < 'A' || c3 > 'Z') return {};
    wchar_t s[4] = { (wchar_t)c1, (wchar_t)c2, (wchar_t)c3, 0 };
    return s;
}

bool QueryActiveConfig(std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
                       std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        LONG rc = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (rc != ERROR_SUCCESS)
            throw DisplayConfigException("GetDisplayConfigBufferSizes failed (error " + std::to_string(rc) + ").");
        paths.assign(pathCount, {});
        modes.assign(modeCount, {});
        rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                &modeCount, modes.data(), nullptr);
        if (rc == ERROR_SUCCESS) {
            paths.resize(pathCount);
            modes.resize(modeCount);
            return true;
        }
        if (rc != ERROR_INSUFFICIENT_BUFFER)
            throw DisplayConfigException("QueryDisplayConfig failed (error " + std::to_string(rc) + ").");
    }
    throw DisplayConfigException(
        "QueryDisplayConfig kept failing with ERROR_INSUFFICIENT_BUFFER \xE2\x80\x94 the display topology is changing.");
}

wstring GetSourceGdiName(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME req{};
    req.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    req.header.size = sizeof(req);
    req.header.adapterId = path.sourceInfo.adapterId;
    req.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&req.header) != ERROR_SUCCESS) return {};
    return req.viewGdiDeviceName;
}

struct TargetNameInfo {
    wstring FriendlyName, DevicePath, EdidManufacturer, EdidManufacturerAlt;
};

TargetNameInfo GetTargetName(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME req{};
    req.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    req.header.size = sizeof(req);
    req.header.adapterId = path.targetInfo.adapterId;
    req.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&req.header) != ERROR_SUCCESS) return {};
    uint16_t raw = req.edidManufactureId;
    uint16_t swapped = (uint16_t)((raw >> 8) | (raw << 8));
    return { req.monitorFriendlyDeviceName, req.monitorDevicePath,
             DecodeVendor(swapped), DecodeVendor(raw) };
}

wstring GetAdapterPath(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_ADAPTER_NAME req{};
    req.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
    req.header.size = sizeof(req);
    req.header.adapterId = path.targetInfo.adapterId;
    req.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&req.header) != ERROR_SUCCESS) return {};
    return req.adapterDevicePath;
}

bool ContainsAnyMarker(const wstring& s) {
    if (s.empty()) return false;
    for (auto* m : kAdapterMarkers)
        if (ContainsIgnoreCase(s, m)) return true;
    return false;
}

bool IsOurVirtualMonitor(const TargetNameInfo& target, const wstring& adapterPath,
                         const std::optional<EdidInfo>& edid) {
    if (DisplayConfig::IsDeskSplitEdid(edid)) return true;
    if (edid && !edid->Manufacturer.empty() &&
        !EqualsIgnoreCase(edid->Manufacturer, DisplayConfig::DeskSplitEdidManufacturer))
        return false;
    if (EqualsIgnoreCase(target.EdidManufacturer, DisplayConfig::DeskSplitEdidManufacturer)) return true;
    if (EqualsIgnoreCase(target.EdidManufacturerAlt, DisplayConfig::DeskSplitEdidManufacturer)) return true;
    if (ContainsAnyMarker(target.FriendlyName)) return true;
    if (ContainsAnyMarker(adapterPath)) return true;
    if (ContainsAnyMarker(target.DevicePath)) return true;
    size_t i = target.DevicePath.find(L"DISPLAY#");
    if (i == wstring::npos) {
        // case-insensitive search
        wstring lower = target.DevicePath;
        for (auto& c : lower) c = towlower(c);
        i = lower.find(L"display#");
        if (i != wstring::npos) {
            wstring tail = target.DevicePath.substr(i + 8);
            if (tail.size() >= 3 && _wcsnicmp(tail.c_str(), L"DSP", 3) == 0) return true;
        }
    } else {
        wstring tail = target.DevicePath.substr(i + 8);
        if (tail.size() >= 3 && _wcsnicmp(tail.c_str(), L"DSP", 3) == 0) return true;
    }
    return false;
}

wstring FallbackName(const wstring& gdiName) {
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(gdiName.c_str(), 0, &dd, 0) && wcslen(dd.DeviceString) > 0)
        return Trim(dd.DeviceString);
    return {};
}

struct ModeSurvey {
    int CurrentWidth = 0, CurrentHeight = 0, PositionX = 0, PositionY = 0;
    int MaxWidth = 0, MaxHeight = 0, MaxRefreshHz = 0, CurrentRefreshHz = 0;
};

ModeSurvey SurveyModes(const wstring& deviceName) {
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    int curW = 0, curH = 0, posX = 0, posY = 0, curHz = 0;
    if (EnumDisplaySettingsExW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &current, 0)) {
        curW = (int)current.dmPelsWidth;
        curH = (int)current.dmPelsHeight;
        posX = current.dmPosition.x;
        posY = current.dmPosition.y;
        curHz = (int)current.dmDisplayFrequency;
    }

    long long bestPixels = 0;
    int maxW = curW, maxH = curH;
    std::map<std::pair<int, int>, int> freqByRes;

    for (int i = 0;; ++i) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsExW(deviceName.c_str(), i, &dm, 0)) break;
        int w = (int)dm.dmPelsWidth;
        int h = (int)dm.dmPelsHeight;
        int hz = (int)dm.dmDisplayFrequency;
        if (w <= 0 || h <= 0) continue;
        if (dm.dmBitsPerPel != 0 && dm.dmBitsPerPel < 32) continue;
        if (dm.dmDisplayFlags & DM_INTERLACED) continue;
        if (hz <= 1) continue;
        long long pixels = (long long)w * h;
        if (pixels > bestPixels) {
            bestPixels = pixels;
            maxW = w;
            maxH = h;
        }
        auto key = std::make_pair(w, h);
        auto it = freqByRes.find(key);
        if (it == freqByRes.end() || hz > it->second) freqByRes[key] = hz;
    }

    int maxHz = curHz;
    auto it = freqByRes.find({ maxW, maxH });
    if (it != freqByRes.end() && it->second > 0) maxHz = it->second;
    if (maxHz <= 1) maxHz = 60;
    if (curW == 0 || curH == 0) { curW = maxW; curH = maxH; }
    if (curHz <= 1) curHz = maxHz;
    return { curW, curH, posX, posY, maxW, maxH, maxHz, curHz };
}

std::map<wstring, DWORD, std::function<bool(const wstring&, const wstring&)>> EnumerateDeviceFlags() {
    // Use a simple vector instead - easier
    return {};
}

struct FlagEntry { wstring name; DWORD flags; };

std::vector<FlagEntry> EnumerateDeviceFlagsList() {
    std::vector<FlagEntry> map;
    for (DWORD i = 0;; ++i) {
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if (dd.DeviceName[0]) map.push_back({ dd.DeviceName, dd.StateFlags });
    }
    return map;
}

DWORD LookupFlags(const std::vector<FlagEntry>& flags, const wstring& name) {
    for (const auto& e : flags)
        if (EqualsIgnoreCase(e.name, name)) return e.flags;
    return 0;
}

} // namespace

uint32_t DisplayConfig::DeskSplitSerialMax() {
    return DeskSplitSerialBase + DESKSPLIT_MAX_MONITORS - 1;
}

bool DisplayConfig::IsDeskSplitEdid(const std::optional<EdidInfo>& edid) {
    if (!edid) return false;
    return EqualsIgnoreCase(edid->Manufacturer, DeskSplitEdidManufacturer) &&
           edid->SerialNumber >= DeskSplitSerialBase &&
           edid->SerialNumber <= DeskSplitSerialMax();
}

int DisplayConfig::DeskSplitIndexFromEdid(const std::optional<EdidInfo>& edid) {
    return IsDeskSplitEdid(edid) ? (int)(edid->SerialNumber - DeskSplitSerialBase) : -1;
}

int DisplayConfig::DeviceIndex(const wstring& deviceName) {
    size_t i = deviceName.rfind(L"DISPLAY");
    if (i == wstring::npos) {
        wstring lower = deviceName;
        for (auto& c : lower) c = towlower(c);
        i = lower.rfind(L"display");
        if (i == wstring::npos) return INT_MAX;
    }
    wstring tail = deviceName.substr(i + 7);
    if (tail.empty()) return INT_MAX;
    try { return std::stoi(tail); } catch (...) { return INT_MAX; }
}

wstring DisplayConfig::DecodeEdidManufacturer(uint16_t value) {
    return DecodeVendor(value);
}

std::vector<MonitorInfo> DisplayConfig::Enumerate() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    QueryActiveConfig(paths, modes);
    auto deviceFlags = EnumerateDeviceFlagsList();
    EdidMap edidMap = EdidReader::BuildMap();

    std::vector<MonitorInfo> ordered;
    std::vector<wstring> seen;

    for (const auto& path : paths) {
        if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0) continue;
        wstring gdiName = GetSourceGdiName(path);
        if (gdiName.empty()) continue;
        bool dup = false;
        for (const auto& s : seen) if (EqualsIgnoreCase(s, gdiName)) { dup = true; break; }
        if (dup) continue;
        seen.push_back(gdiName);

        TargetNameInfo target = GetTargetName(path);
        wstring adapterPath = GetAdapterPath(path);
        ModeSurvey survey = SurveyModes(gdiName);
        uint32_t currentRefreshMhz = ToMillihertz(path.targetInfo.refreshRate);

        uint32_t maxRefreshMhz;
        if (currentRefreshMhz != 0 &&
            (int)std::lround(currentRefreshMhz / 1000.0) == survey.MaxRefreshHz)
            maxRefreshMhz = currentRefreshMhz;
        else
            maxRefreshMhz = (uint32_t)(std::max)(1, survey.MaxRefreshHz) * 1000u;

        DWORD flags = LookupFlags(deviceFlags, gdiName);
        std::optional<EdidInfo> edid;
        EdidInfo found;
        if (EdidReader::TryLookup(edidMap, target.DevicePath, found))
            edid = found;

        uint32_t currentMillihertz = currentRefreshMhz != 0
            ? currentRefreshMhz
            : (uint32_t)(std::max)(0, survey.CurrentRefreshHz) * 1000u;

        MonitorInfo info;
        info.DeviceName = gdiName;
        info.Index = DeviceIndex(gdiName);
        info.AdapterIdLow = path.targetInfo.adapterId.LowPart;
        info.AdapterIdHigh = path.targetInfo.adapterId.HighPart;
        info.TargetId = path.targetInfo.id;
        wstring fn = Trim(target.FriendlyName);
        info.FriendlyName = fn.empty() ? FallbackName(gdiName) : fn;
        info.AdapterPath = adapterPath;
        info.MonitorDevicePath = target.DevicePath;
        if (edid && !edid->Manufacturer.empty())
            info.EdidManufacturer = edid->Manufacturer;
        else
            info.EdidManufacturer = target.EdidManufacturer;
        info.Edid = edid;
        info.DeskSplitMonitorIndex = DeskSplitIndexFromEdid(edid);
        info.CurrentWidth = survey.CurrentWidth;
        info.CurrentHeight = survey.CurrentHeight;
        info.PositionX = survey.PositionX;
        info.PositionY = survey.PositionY;
        info.MaxWidth = survey.MaxWidth;
        info.MaxHeight = survey.MaxHeight;
        info.MaxRefreshHz = survey.MaxRefreshHz;
        info.MaxRefreshMillihertz = maxRefreshMhz;
        info.CurrentRefreshHz = survey.CurrentRefreshHz;
        info.CurrentRefreshMillihertz = currentMillihertz;
        info.IsPrimary = (flags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        info.IsDeskSplitVirtual = IsOurVirtualMonitor(target, adapterPath, edid);
        ordered.push_back(std::move(info));
    }

    std::sort(ordered.begin(), ordered.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.Index != b.Index) return a.Index < b.Index;
        return _wcsicmp(a.DeviceName.c_str(), b.DeviceName.c_str()) < 0;
    });
    return ordered;
}

std::vector<MonitorInfo> DisplayConfig::EnumeratePhysical() {
    auto all = Enumerate();
    std::vector<MonitorInfo> out;
    for (auto& m : all) if (!m.IsDeskSplitVirtual) out.push_back(std::move(m));
    return out;
}

std::vector<MonitorInfo> DisplayConfig::EnumerateVirtual() {
    auto all = Enumerate();
    std::vector<MonitorInfo> out;
    for (auto& m : all) if (m.IsDeskSplitVirtual) out.push_back(std::move(m));
    return out;
}

wstring DispChangeToString(int code) {
    switch (code) {
    case DISP_CHANGE_SUCCESSFUL: return L"successful";
    case DISP_CHANGE_RESTART: return L"restart required";
    case DISP_CHANGE_FAILED: return L"failed";
    case DISP_CHANGE_BADMODE: return L"mode not supported";
    case DISP_CHANGE_NOTUPDATED: return L"could not write settings to the registry";
    case DISP_CHANGE_BADFLAGS: return L"invalid flags";
    case DISP_CHANGE_BADPARAM: return L"invalid parameter";
#ifdef DISP_CHANGE_BADDUALVIEW
    case DISP_CHANGE_BADDUALVIEW: return L"not supported in dual-view mode";
#endif
    default: return L"unknown code " + std::to_wstring(code);
    }
}

} // namespace ds
