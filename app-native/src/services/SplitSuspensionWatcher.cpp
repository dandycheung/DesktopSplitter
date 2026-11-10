#include "services/SplitSuspensionWatcher.h"
#include "services/ApplyService.h"
#include "services/HidingStatus.h"

namespace ds {

SplitSuspensionWatcher::SplitSuspensionWatcher(std::function<void(const wstring&)> log)
    : log_(std::move(log)) {
    InitializeCriticalSection(&stateGate_);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

SplitSuspensionWatcher::~SplitSuspensionWatcher() {
    Stop(false);
    if (stopEvent_) CloseHandle(stopEvent_);
    DeleteCriticalSection(&stateGate_);
}

DWORD WINAPI SplitSuspensionWatcher::ThreadProc(LPVOID self) {
    static_cast<SplitSuspensionWatcher*>(self)->Loop();
    return 0;
}

void SplitSuspensionWatcher::Start(const wstring& activePrimaryDevice, const wstring& suspendedPrimaryDevice) {
    EnterCriticalSection(&stateGate_);
    activePrimaryDevice_ = activePrimaryDevice;
    suspendedPrimaryDevice_ = suspendedPrimaryDevice;
    if (thread_) { LeaveCriticalSection(&stateGate_); return; }
    if (activePrimaryDevice_.empty() || suspendedPrimaryDevice_.empty()) {
        LeaveCriticalSection(&stateGate_);
        if (log_) log_(L"Suspend/resume watcher not started: the primary displays are not known.");
        return;
    }
    ResetEvent(stopEvent_);
    isSuspended_ = false;
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    LeaveCriticalSection(&stateGate_);
    if (log_) log_(L"Watching for lock / UAC transitions (primary will move to " +
                   suspendedPrimaryDevice_ + L" while they are on screen).");
}

void SplitSuspensionWatcher::Stop(bool restorePrimary) {
    HANDLE thread = nullptr;
    EnterCriticalSection(&stateGate_);
    thread = thread_;
    thread_ = nullptr;
    LeaveCriticalSection(&stateGate_);
    if (!thread) return;
    SetEvent(stopEvent_);
    WaitForSingleObject(thread, 3000);
    CloseHandle(thread);
    if (restorePrimary && isSuspended_) {
        if (log_) log_(L"Restoring the split's primary display before stopping the watcher.");
        MoveTo(activePrimaryDevice_, L"segment 1");
    }
    isSuspended_ = false;
    CloseHandles();
    if (log_) log_(L"Stopped watching for lock / UAC transitions.");
}

HANDLE SplitSuspensionWatcher::TryOpen(const wchar_t* name) {
    const wchar_t* prefixes[] = { L"Global\\", L"Local\\" };
    for (auto* prefix : prefixes) {
        wstring full = wstring(prefix) + name;
        HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, full.c_str());
        if (h) return h;
    }
    return nullptr;
}

void SplitSuspensionWatcher::EnsureHandles() {
    if (!suspend_) suspend_ = TryOpen(SuspendEventName);
    if (!resume_) resume_ = TryOpen(ResumeEventName);
}

void SplitSuspensionWatcher::CloseHandles() {
    if (suspend_) { CloseHandle(suspend_); suspend_ = nullptr; }
    if (resume_) { CloseHandle(resume_); resume_ = nullptr; }
}

SuspensionRequest SplitSuspensionWatcher::ReadDesiredState() {
    EnsureHandles();
    bool sus = suspend_ && WaitForSingleObject(suspend_, 0) == WAIT_OBJECT_0;
    bool res = resume_ && WaitForSingleObject(resume_, 0) == WAIT_OBJECT_0;
    if (sus && !res) return SuspensionRequest::Suspended;
    if (res && !sus) return SuspensionRequest::Active;
    if (!suspend_ && !resume_) return SuspensionRequest::Unknown;
    return SuspensionRequest::Active;
}

bool SplitSuspensionWatcher::MoveTo(const wstring& device, const wstring& description) {
    if (device.empty()) return false;
    std::lock_guard<std::mutex> lock(ApplyService::DisplayOperationGate);
    if (!HidingStatus::IsSplitActive()) {
        if (log_) log_(L"Split is no longer active \u2014 skipping the primary switch.");
        return false;
    }
    return ApplyService::SwitchPrimaryTo(device, description, log_);
}

void SplitSuspensionWatcher::Reconcile() {
    SuspensionRequest desired = ReadDesiredState();
    if (desired == SuspensionRequest::Unknown) {
        if (isSuspended_ && HidingStatus::IsSplitActive()) {
            if (log_) log_(L"Compositor events disappeared while suspended \u2014 restoring the split's primary.");
            if (MoveTo(activePrimaryDevice_, L"segment 1")) isSuspended_ = false;
        }
        return;
    }
    bool wantSuspended = desired == SuspensionRequest::Suspended;
    if (wantSuspended == isSuspended_) return;
    if (!HidingStatus::IsSplitActive()) {
        isSuspended_ = false;
        return;
    }
    if (wantSuspended) {
        if (log_) log_(L"Lock screen / secure desktop detected \u2014 handing primary back to " +
                       suspendedPrimaryDevice_ + L".");
        if (MoveTo(suspendedPrimaryDevice_, L"the physical monitor")) isSuspended_ = true;
    } else {
        if (log_) log_(L"Secure desktop dismissed \u2014 returning primary to " + activePrimaryDevice_ + L".");
        if (MoveTo(activePrimaryDevice_, L"segment 1")) isSuspended_ = false;
    }
}

void SplitSuspensionWatcher::Loop() {
    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        try {
            EnsureHandles();
            HANDLE target = isSuspended_ ? resume_ : suspend_;
            HANDLE waits[2] = { stopEvent_, target ? target : stopEvent_ };
            WaitForMultipleObjects(target ? 2 : 1, waits, FALSE, 2000);
            if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;
            Reconcile();
        } catch (const std::exception& ex) {
            if (log_) log_(L"Suspend/resume watcher error: " + Widen(ex.what()));
            WaitForSingleObject(stopEvent_, 2000);
        }
    }
}

} // namespace ds
