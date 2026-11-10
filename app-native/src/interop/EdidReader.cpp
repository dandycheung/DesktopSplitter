#include "interop/EdidReader.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <regstr.h>

namespace ds {
namespace {

const GUID kGuidDevInterfaceMonitor =
    { 0xe6f07b5f, 0xee97, 0x4a90, { 0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7 } };
const GUID kGuidDevClassMonitor =
    { 0x4d36e96e, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

wstring DecodeVendor(uint16_t value) {
    int c1 = ((value >> 10) & 0x1F) + 'A' - 1;
    int c2 = ((value >> 5) & 0x1F) + 'A' - 1;
    int c3 = (value & 0x1F) + 'A' - 1;
    if (c1 < 'A' || c1 > 'Z' || c2 < 'A' || c2 > 'Z' || c3 < 'A' || c3 > 'Z') return {};
    wchar_t s[4] = { (wchar_t)c1, (wchar_t)c2, (wchar_t)c3, 0 };
    return s;
}

std::vector<uint8_t> ReadBinaryValue(HKEY key, const wchar_t* valueName) {
    DWORD size = 0, type = 0;
    LONG rc = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA) return {};
    if (size == 0) return {};
    std::vector<uint8_t> data(size);
    rc = RegQueryValueExW(key, valueName, nullptr, &type, data.data(), &size);
    if (rc != ERROR_SUCCESS) return {};
    data.resize(size);
    return data;
}

std::optional<EdidInfo> ReadEdid(HDEVINFO set, SP_DEVINFO_DATA& devInfo) {
    HKEY key = SetupDiOpenDevRegKey(set, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE || key == nullptr) return std::nullopt;
    auto blob = ReadBinaryValue(key, L"EDID");
    RegCloseKey(key);
    return EdidReader::Parse(blob.empty() ? nullptr : blob.data(), blob.size());
}

wstring GetDeviceInstanceId(HDEVINFO set, SP_DEVINFO_DATA& devInfo) {
    wchar_t buffer[512];
    DWORD required = 0;
    if (!SetupDiGetDeviceInstanceIdW(set, &devInfo, buffer, 512, &required)) return {};
    return buffer;
}

wstring GetInterfaceDetail(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& ifData, SP_DEVINFO_DATA& devInfo) {
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &required, nullptr);
    if (required == 0) return {};
    std::vector<uint8_t> buffer(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, required, nullptr, &devInfo))
        return {};
    return detail->DevicePath;
}

void AddFromInterfaceEnumeration(EdidMap& map) {
    GUID interfaceGuid = kGuidDevInterfaceMonitor;
    HDEVINFO set = SetupDiGetClassDevsW(&interfaceGuid, nullptr, nullptr,
                                        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA ifData{};
        ifData.cbSize = sizeof(ifData);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &interfaceGuid, i, &ifData)) break;
        SP_DEVINFO_DATA devInfo{};
        devInfo.cbSize = sizeof(devInfo);
        wstring path = GetInterfaceDetail(set, ifData, devInfo);
        auto value = ReadEdid(set, devInfo);
        if (!value) continue;
        if (!path.empty()) map[path] = *value;
        wstring instanceId = GetDeviceInstanceId(set, devInfo);
        if (!instanceId.empty()) map[instanceId] = *value;
    }
    SetupDiDestroyDeviceInfoList(set);
}

void AddFromClassEnumeration(EdidMap& map) {
    GUID classGuid = kGuidDevClassMonitor;
    HDEVINFO set = SetupDiGetClassDevsW(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA devInfo{};
        devInfo.cbSize = sizeof(devInfo);
        if (!SetupDiEnumDeviceInfo(set, i, &devInfo)) break;
        wstring instanceId = GetDeviceInstanceId(set, devInfo);
        if (instanceId.empty() || map.find(instanceId) != map.end()) continue;
        auto value = ReadEdid(set, devInfo);
        if (value) map[instanceId] = *value;
    }
    SetupDiDestroyDeviceInfoList(set);
}

} // namespace

std::optional<EdidInfo> EdidReader::Parse(const uint8_t* blob, size_t length) {
    if (!blob || length < 16) return std::nullopt;
    if (blob[0] != 0x00 || blob[7] != 0x00) return std::nullopt;
    for (int i = 1; i <= 6; ++i) if (blob[i] != 0xFF) return std::nullopt;
    uint16_t vendor = (uint16_t)((blob[8] << 8) | blob[9]);
    uint16_t product = (uint16_t)(blob[10] | (blob[11] << 8));
    uint32_t serial = (uint32_t)(blob[12] | (blob[13] << 8) | (blob[14] << 16) | (blob[15] << 24));
    return EdidInfo{ DecodeVendor(vendor), product, serial };
}

wstring EdidReader::DeviceInstanceIdFromInterfacePath(const wstring& interfacePath) {
    if (interfacePath.empty()) return {};
    wstring s = interfacePath;
    if (s.size() >= 4 &&
        ((s[0] == L'\\' && s[1] == L'\\' && (s[2] == L'?' || s[2] == L'.') && s[3] == L'\\')))
        s = s.substr(4);

    std::vector<wstring> parts;
    size_t start = 0;
    for (;;) {
        size_t h = s.find(L'#', start);
        if (h == wstring::npos) { parts.push_back(s.substr(start)); break; }
        parts.push_back(s.substr(start, h - start));
        start = h + 1;
    }
    int take = (int)parts.size();
    if (take > 0 && !parts[take - 1].empty() && parts[take - 1][0] == L'{') take--;
    if (take <= 0) return {};
    wstring out;
    for (int i = 0; i < take; ++i) {
        if (i) out += L'\\';
        out += parts[i];
    }
    return out;
}

bool EdidReader::TryLookup(const EdidMap& map, const wstring& monitorDevicePath, EdidInfo& edid) {
    if (monitorDevicePath.empty() || map.empty()) return false;
    auto it = map.find(monitorDevicePath);
    if (it != map.end()) { edid = it->second; return true; }
    wstring instanceId = DeviceInstanceIdFromInterfacePath(monitorDevicePath);
    if (!instanceId.empty()) {
        it = map.find(instanceId);
        if (it != map.end()) { edid = it->second; return true; }
    }
    return false;
}

bool EdidReader::TryRead(const wstring& monitorDevicePath, EdidInfo& edid) {
    return TryLookup(BuildMap(), monitorDevicePath, edid);
}

EdidMap EdidReader::BuildMap() {
    EdidMap map;
    try {
        AddFromInterfaceEnumeration(map);
        AddFromClassEnumeration(map);
    } catch (...) {}
    return map;
}

} // namespace ds
