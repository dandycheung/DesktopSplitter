#pragma once
#include "Common.h"
#include <cfgmgr32.h>
#include <guiddef.h>
#include "DeskSplitProtocol.h"

namespace ds {

class DriverNotFoundException : public std::runtime_error {
public:
    explicit DriverNotFoundException(const string& m) : std::runtime_error(m) {}
};
class DriverIoException : public std::runtime_error {
public:
    explicit DriverIoException(const string& m) : std::runtime_error(m) {}
};

class DriverClient {
public:
    static constexpr const wchar_t* DriverMissingMessage =
        L"Driver device not found \u2014 install the driver first (installer\\install-driver.ps1).";

    static bool TryFindDevicePath(wstring& devicePath);
    static bool IsDriverPresent() { wstring p; return TryFindDevicePath(p); }
    static wstring GetDevicePath();
    static void SetConfig(const DESKSPLIT_CONFIG& config);
    static DESKSPLIT_STATUS GetStatus();
    static void ClearConfig();
    static DESKSPLIT_CONFIG NewConfig();
};

} // namespace ds
