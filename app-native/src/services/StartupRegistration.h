#pragma once
#include "Common.h"

namespace ds {

struct StartupState {
    bool Enabled = false;
    wstring RegisteredCommand;
    bool PointsAtCurrent = false;
    bool NeedsRefresh() const { return Enabled && !PointsAtCurrent; }
};

class StartupRegistration {
public:
    static constexpr const wchar_t* StartupArgument = L"--startup";

    static wstring CurrentExecutablePath();
    static wstring ExpectedCommand();
    static bool LaunchedAtStartup(int argc, wchar_t** argv);
    static StartupState Read();
    static bool IsEnabled() { return Read().Enabled; }
    static bool Enable(const std::function<void(const wstring&)>& log = {});
    static bool Disable(const std::function<void(const wstring&)>& log = {});
    static bool Set(bool enabled, const std::function<void(const wstring&)>& log = {});
    static void RefreshIfStale(const std::function<void(const wstring&)>& log = {});
    static wstring ExtractExecutable(const wstring& command);
};

} // namespace ds
