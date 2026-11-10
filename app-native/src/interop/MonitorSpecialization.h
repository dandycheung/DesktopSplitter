#pragma once
#include "models/Types.h"

namespace ds {

// DISPLAYCONFIG_DEVICE_INFO_GET/SET_MONITOR_SPECIALIZATION — in wingdi.h on recent SDKs
// but we define the packets ourselves so the binary still builds against older headers.
#ifndef DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION
#define DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION  12
#endif
#ifndef DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION
#define DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION  13
#endif

#pragma pack(push, 8)
struct DS_DISPLAYCONFIG_GET_MONITOR_SPECIALIZATION {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;
};

struct DS_DISPLAYCONFIG_SET_MONITOR_SPECIALIZATION {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;
    GUID specializationType;
    GUID specializationSubType;
    WCHAR specializationApplicationName[128];
};
#pragma pack(pop)

struct SpecializationState {
    uint32_t RawValue = 0;
    bool EnabledNow() const { return (RawValue & 0x1) != 0; }
    bool AvailableForMonitor() const { return (RawValue & 0x2) != 0; }
    bool AvailableForSystem() const { return (RawValue & 0x4) != 0; }
    bool Available() const { return AvailableForMonitor() && AvailableForSystem(); }
};

struct SpecializationResult {
    bool Success = false;
    int ErrorCode = 0;
    wstring Message;
    bool AccessDenied() const { return ErrorCode == 5; }
};

class MonitorSpecialization {
public:
    static constexpr int ERROR_ACCESS_DENIED_ = 5;
    static constexpr int ERROR_INVALID_PARAMETER_ = 87;
    static constexpr int ERROR_NOT_SUPPORTED_ = 50;
    static constexpr int ERROR_GEN_FAILURE_ = 31;

    static bool TryGet(const MonitorInfo& monitor, SpecializationState& state, int& errorCode);
    static std::optional<SpecializationState> Get(const MonitorInfo& monitor);
    static SpecializationResult Set(const MonitorInfo& monitor, bool enabled);
};

} // namespace ds
