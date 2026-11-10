#pragma once
#include "models/Types.h"
#include "interop/EdidReader.h"

namespace ds {

class DisplayConfigException : public std::runtime_error {
public:
    explicit DisplayConfigException(const string& m) : std::runtime_error(m) {}
};

class DisplayConfig {
public:
    static constexpr const wchar_t* DeskSplitEdidManufacturer = L"DSP";
    static constexpr uint32_t DeskSplitSerialBase = 0xD5000001;
    static uint32_t DeskSplitSerialMax();

    static bool IsDeskSplitEdid(const std::optional<EdidInfo>& edid);
    static int DeskSplitIndexFromEdid(const std::optional<EdidInfo>& edid);

    static std::vector<MonitorInfo> Enumerate();
    static std::vector<MonitorInfo> EnumeratePhysical();
    static std::vector<MonitorInfo> EnumerateVirtual();
    static int DeviceIndex(const wstring& deviceName);

    static wstring DecodeEdidManufacturer(uint16_t value);
};

wstring DispChangeToString(int code);

} // namespace ds
