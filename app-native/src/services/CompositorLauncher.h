#pragma once
#include "Common.h"

namespace ds {

class CompositorNotFoundException : public std::runtime_error {
public:
    explicit CompositorNotFoundException(const string& m) : std::runtime_error(m) {}
};

enum class SpecializedWait { Live, CompositorExited, TimedOut };

class CompositorLauncher {
public:
    static constexpr const wchar_t* ExecutableName = L"dscomp.exe";
    static constexpr const wchar_t* StopEventName = L"Global\\DeskSplitCompositorStop";
    static constexpr const wchar_t* SpecializedLiveEventName = L"Global\\DeskSplitSpecializedLive";
    static constexpr const wchar_t* SpecializedArgument = L"--specialized";
    static constexpr const wchar_t* WaitForTargetArgument = L"--wait-for-target";
    static constexpr const wchar_t* FrameDeadlineArgument = L"--frame-deadline";
    static constexpr int WaitForTargetSeconds = 90;
    static constexpr int FrameDeadlineSeconds = 10;

    static wstring ExpectedPath();
    static bool TryFindExecutable(wstring& path);
    static bool IsRunning();
    static std::vector<DWORD> GetRunningPids();
    static SpecializedWait WaitForSpecializedLive(DWORD pid, int timeoutMs, HANDLE cancelEvent,
                                                  const std::function<void(const wstring&)>& log = {});
    static DWORD Start(const std::vector<wstring>& arguments = {});
    static bool SignalStop();
    static bool StopAndWait(int timeoutMs, const std::function<void(const wstring&)>& log = {});
    static wstring BaseDirectory();
};

} // namespace ds
