#pragma once
#include "Common.h"

namespace ds {

enum class SuspensionRequest { Unknown, Active, Suspended };

class SplitSuspensionWatcher {
public:
    static constexpr const wchar_t* SuspendEventName = L"DeskSplitSuspendRequest";
    static constexpr const wchar_t* ResumeEventName = L"DeskSplitResumeRequest";

    explicit SplitSuspensionWatcher(std::function<void(const wstring&)> log);
    ~SplitSuspensionWatcher();

    void Start(const wstring& activePrimaryDevice, const wstring& suspendedPrimaryDevice);
    void Stop(bool restorePrimary);

    bool IsSuspended() const { return isSuspended_; }
    bool IsRunning() const { return thread_ != nullptr; }
    SuspensionRequest ReadDesiredState();
    void Reconcile();

private:
    void Loop();
    bool MoveTo(const wstring& device, const wstring& description);
    void EnsureHandles();
    void CloseHandles();
    static HANDLE TryOpen(const wchar_t* name);

    std::function<void(const wstring&)> log_;
    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE suspend_ = nullptr;
    HANDLE resume_ = nullptr;
    CRITICAL_SECTION stateGate_{};
    wstring activePrimaryDevice_;
    wstring suspendedPrimaryDevice_;
    bool isSuspended_ = false;
    static DWORD WINAPI ThreadProc(LPVOID self);
};

} // namespace ds
