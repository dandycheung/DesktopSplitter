#include "services/ApplyService.h"
#include "services/SettingsStore.h"
#include "services/HidingStatus.h"
#include "services/CompositorLauncher.h"
#include "services/SpecializationSupport.h"
#include "interop/DriverClient.h"
#include "interop/DisplayConfig.h"
#include "interop/DisplayArranger.h"
#include "interop/MonitorSpecialization.h"
#include <chrono>
#include <winioctl.h>

namespace ds {

std::mutex ApplyService::DisplayOperationGate;

namespace {

bool Cancelled(HANDLE ev) {
    return ev && WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
}

void ThrowIfCancelled(HANDLE ev) {
    if (Cancelled(ev)) throw ApplyException("Operation cancelled.");
}

void SleepPoll(int ms, HANDLE ev) {
    if (!ev) { Sleep(ms); return; }
    WaitForSingleObject(ev, ms);
}

bool SameDevice(const wstring& a, const wstring& b) {
    return !a.empty() && !b.empty() && EqualsIgnoreCase(a, b);
}

std::vector<MonitorInfo> SafeEnumerate() {
    try { return DisplayConfig::Enumerate(); }
    catch (const DisplayConfigException&) { return {}; }
}

const MonitorInfo* FindLive(const std::vector<MonitorInfo>& all, const wstring& device) {
    for (const auto& m : all) if (SameDevice(m.DeviceName, device)) return &m;
    return nullptr;
}

MonitorInfo* FindLiveMut(std::vector<MonitorInfo>& all, const wstring& device) {
    for (auto& m : all) if (SameDevice(m.DeviceName, device)) return &m;
    return nullptr;
}

wstring IsoNowUtc() {
    SYSTEMTIME st; GetSystemTime(&st);
    wchar_t buf[40];
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void TryForceExtendTopology() {
    try {
        SetDisplayConfig(0, nullptr, 0, nullptr, SDC_TOPOLOGY_EXTEND | SDC_APPLY);
    } catch (...) {}
}

bool TryMapByEdid(const std::vector<MonitorInfo>& arrived, int segmentCount,
                  std::vector<MonitorInfo>& mapped, wstring& reason) {
    std::vector<const MonitorInfo*> byIndex(segmentCount, nullptr);
    bool duplicates = false;
    for (const auto& m : arrived) {
        int index = m.DeskSplitMonitorIndex;
        if (index < 0 || index >= segmentCount) continue;
        if (byIndex[index]) duplicates = true;
        else byIndex[index] = &m;
    }
    int count = 0;
    for (auto* p : byIndex) if (p) ++count;
    if (!duplicates && count == segmentCount) {
        mapped.clear();
        for (auto* p : byIndex) mapped.push_back(*p);
        reason.clear();
        return true;
    }
    mapped.clear();
    if (duplicates) reason = L"two virtual monitors reported the same driver index";
    else if (count == 0) reason = L"no EDID serial in the DesktopSplitter range could be read";
    else reason = L"only " + std::to_wstring(count) + L" of " + std::to_wstring(segmentCount) +
                  L" monitors reported a usable driver index";
    return false;
}

std::vector<MonitorInfo> WaitForVirtualMonitors(const std::vector<wstring>& baseline, int expected, HANDLE ev) {
    const int timeoutMs = 15000;
    const int pollMs = 400;
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    ULONGLONG start = GetTickCount64();
    bool triedExtend = false;
    std::vector<MonitorInfo> found;

    auto collect = [&]() {
        std::vector<MonitorInfo> out;
        for (const auto& m : SafeEnumerate()) {
            bool inBase = false;
            for (const auto& b : baseline)
                if (EqualsIgnoreCase(b, m.DeviceName)) { inBase = true; break; }
            if (m.IsDeskSplitVirtual || !inBase) out.push_back(m);
        }
        std::sort(out.begin(), out.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
            int ia = DisplayConfig::DeviceIndex(a.DeviceName);
            int ib = DisplayConfig::DeviceIndex(b.DeviceName);
            if (ia != ib) return ia < ib;
            return _wcsicmp(a.DeviceName.c_str(), b.DeviceName.c_str()) < 0;
        });
        return out;
    };

    while (GetTickCount64() < deadline) {
        ThrowIfCancelled(ev);
        found = collect();
        if ((int)found.size() == expected) return found;
        if (!triedExtend && (GetTickCount64() - start) >= 1000) {
            triedExtend = true;
            TryForceExtendTopology();
        }
        SleepPoll(pollMs, ev);
    }
    TryForceExtendTopology();
    found = collect();
    if ((int)found.size() == expected) return found;
    throw ApplyException(
        "Timed out after 15s waiting for the virtual monitors: expected " + std::to_string(expected) +
        ", saw " + std::to_string(found.size()) +
        ". Is the DesktopSplitter driver installed and started (Device Manager -> Display adapters)?");
}

std::vector<MonitorInfo> MapSegmentsToMonitors(const std::vector<wstring>& baseline,
                                               std::vector<MonitorInfo> arrived, int segmentCount,
                                               const LogFn& log, HANDLE ev) {
    ULONGLONG deadline = GetTickCount64() + 3000;
    wstring reason;
    for (;;) {
        std::vector<MonitorInfo> mapped;
        if (TryMapByEdid(arrived, segmentCount, mapped, reason)) {
            log(L"Mapped segments to virtual monitors by EDID serial (driver monitor index).");
            for (int i = 0; i < (int)mapped.size(); ++i) {
                const auto& edid = mapped[i].Edid;
                wchar_t line[256];
                swprintf_s(line, L"  Segment %d -> %s (serial 0x%08X, product 0x%04X)",
                           i + 1, mapped[i].DeviceName.c_str(),
                           edid ? edid->SerialNumber : 0, edid ? edid->ProductCode : 0);
                log(line);
                if (edid && edid->ProductCode != (uint16_t)i) {
                    log(L"  Note: EDID product code 0x" + [&](){
                        wchar_t b[16]; swprintf_s(b, L"%04X", edid->ProductCode); return wstring(b);
                    }() + L" does not match monitor index " + std::to_wstring(i) + L"; trusting the serial.");
                }
            }
            return mapped;
        }
        if (GetTickCount64() >= deadline || Cancelled(ev)) break;
        SleepPoll(400, ev);
        auto refreshed = WaitForVirtualMonitors(baseline, (int)arrived.size(), ev);
        // Don't wait full arrival timeout here — just re-enumerate quickly.
        std::vector<MonitorInfo> now;
        for (const auto& m : SafeEnumerate()) {
            bool inBase = false;
            for (const auto& b : baseline)
                if (EqualsIgnoreCase(b, m.DeviceName)) { inBase = true; break; }
            if (m.IsDeskSplitVirtual || !inBase) now.push_back(m);
        }
        if (now.size() == arrived.size()) arrived = std::move(now);
    }
    log(L"WARNING: " + reason + L" \u2014 falling back to positional \\\\.\\DISPLAYn-order mapping. "
        L"Segments may end up on the wrong virtual monitor.");
    return arrived;
}

DWORD StartPlain(const LogFn& log) {
    DWORD pid = CompositorLauncher::Start();
    log(L"Started dscomp.exe (pid " + std::to_wstring(pid) + L").");
    return pid;
}

bool SwitchPrimary(const wstring& targetDevice, const wstring& description, const LogFn& log) {
    if (targetDevice.empty()) {
        log(L"WARNING: no target display for the primary switch; leaving it unchanged.");
        return false;
    }
    auto current = SafeEnumerate();
    if (current.empty()) {
        log(L"WARNING: could not enumerate displays; primary display left unchanged.");
        return false;
    }
    const MonitorInfo* livePrimary = nullptr;
    for (const auto& m : current) if (m.IsPrimary) { livePrimary = &m; break; }
    if (livePrimary && SameDevice(livePrimary->DeviceName, targetDevice)) {
        log(description + L" is already the primary display.");
        return true;
    }
    auto snapshot = DisplayArranger::SnapshotLayout(current);
    const MonitorPlacement* origin = nullptr;
    for (const auto& p : snapshot) if (SameDevice(p.DeviceName, targetDevice)) { origin = &p; break; }
    if (!origin) {
        log(L"WARNING: " + targetDevice + L" is not in the current display set; primary left unchanged.");
        return false;
    }
    try {
        auto rebased = DisplayArranger::RebaseToPrimary(snapshot, targetDevice);
        log(L"Making " + description + L" the primary display \u2014 re-basing all " +
            std::to_wstring(rebased.size()) + L" monitor(s) by (" +
            std::to_wstring(-origin->X) + L"," + std::to_wstring(-origin->Y) + L"):");
        DisplayArranger::ApplyLayout(rebased, log);
        return true;
    } catch (const DisplayArrangeException& ex) {
        log(L"WARNING: primary switch failed \u2014 " + Widen(ex.what()));
        return false;
    }
}

void RestorePrimaryBeforeTeardown(const LogFn& log) {
    std::vector<MonitorInfo> physical;
    for (const auto& m : SafeEnumerate()) if (!m.IsDeskSplitVirtual) physical.push_back(m);
    if (physical.empty()) {
        log(L"WARNING: no physical monitors found; leaving the primary display alone.");
        return;
    }
    auto settings = SettingsStore::Load();
    const MonitorInfo* target = nullptr;
    wstring why;
    if (settings) {
        for (const auto& m : physical)
            if (SameDevice(m.DeviceName, settings->PreviousPrimaryDevice)) { target = &m; why = L"primary before the split"; break; }
        if (!target && !settings->PreviousPrimaryFriendlyName.empty()) {
            for (const auto& m : physical)
                if (SameDevice(m.FriendlyName, settings->PreviousPrimaryFriendlyName)) {
                    target = &m; why = L"primary before the split, matched by name"; break;
                }
        }
        if (!target) {
            for (const auto& m : physical)
                if (SameDevice(m.DeviceName, settings->PhysicalDevice)) {
                    target = &m; why = L"the monitor that was split; no saved previous primary"; break;
                }
        }
    }
    if (!target) {
        for (const auto& m : physical) if (m.IsPrimary) { target = &m; break; }
        if (!target) target = &physical[0];
        why = L"first available physical monitor; no usable saved state";
    }
    SwitchPrimary(target->DeviceName,
                  target->DisplayLabel() + L" (" + target->DeviceName + L") [" + why + L"]", log);
}

bool SetSpecialized(const MonitorInfo& physical, bool enable, ISpecializationUi* ui,
                    const LogFn& log, HANDLE ev);

void RestoreToDesktop(const MonitorInfo& physical, ISpecializationUi* ui, const LogFn& log, HANDLE ev) {
    if (SetSpecialized(physical, false, ui, log, ev))
        log(physical.DisplayLabel() + L" is back on the desktop.");
    else
        log(L"WARNING: " + physical.DisplayLabel() + L" may still be off the desktop.");
}

bool SetSpecialized(const MonitorInfo& physical, bool enable, ISpecializationUi* ui,
                    const LogFn& log, HANDLE ev) {
    wstring what = enable ? L"Removing" : L"Restoring";
    log(what + L" " + physical.DisplayLabel() + (enable ? L" from" : L" to") + L" the desktop...");
    AdminFlowResult admin = SystemSettingsAdminFlowsClient::TryRun(physical, enable, log, ev);
    if (admin.Success) { log(admin.Message); return true; }
    if (admin.NotAttempted) log(L"Automatic route unavailable: " + admin.Message);
    else log(L"Automatic route failed: " + admin.Message);
    log(L"Using the Windows Settings route.");
    if (!ui) {
        log(L"Do it yourself: Settings > System > Display > Advanced display > select " +
            physical.DisplayLabel() + L" > turn " + (enable ? L"ON" : L"OFF") +
            L" \"Remove display from desktop\".");
        return false;
    }
    SpecializationCaptureWatcher watcher(physical, log);
    watcher.Start();
    SpecializationResult failure{ false, 0, admin.Message };
    bool reached = false;
    if (enable) {
        reached = ui->GuideManualEnable(physical, failure, ev);
    } else {
        ui->GuideManualDisable(physical, failure, ev);
        SpecializationState after;
        int rc = 0;
        reached = MonitorSpecialization::TryGet(physical, after, rc) && !after.EnabledNow();
    }
    auto captured = watcher.StopAndCollect();
    if (captured) SpecializationHashStore::Learn(physical, captured->Hash, log);
    else if (watcher.ElevationRequired()) {
        log(L"Note: Windows ran its display helper, but DesktopSplitter could not read how "
            L"it was called because that helper runs as administrator. Run DesktopSplitter "
            L"as administrator once while you do this, and it will remember the setting and "
            L"be able to do it for you in one click from then on.");
    }
    return reached;
}

std::pair<DWORD, wstring> LaunchCompositor(const ApplyRequest& request, const LogFn& log, HANDLE ev) {
    if (!request.HidePhysicalDisplay) return { StartPlain(log), L"" };
    auto all = SafeEnumerate();
    const MonitorInfo* physical = FindLive(all, request.Physical.DeviceName);
    if (!physical) {
        log(L"WARNING: " + request.Physical.DeviceName + L" disappeared; starting the compositor normally.");
        return { StartPlain(log), L"" };
    }
    if (physical->IsPrimary) {
        log(L"WARNING: " + physical->DisplayLabel() + L" is still the primary display, and Windows only "
            L"allows non-primary displays to be hidden. Skipping hiding.");
        return { StartPlain(log), L"" };
    }
    SpecializationState state; int rc = 0;
    if (!MonitorSpecialization::TryGet(*physical, state, rc)) {
        log(L"WARNING: could not read monitor specialization (error " + std::to_wstring(rc) + L"). Skipping hiding.");
        return { StartPlain(log), L"" };
    }
    if (!state.Available()) {
        wchar_t raw[16]; swprintf_s(raw, L"%X", state.RawValue);
        log(L"WARNING: " + physical->DisplayLabel() + L" cannot be hidden (GET raw 0x" + raw +
            L": monitor=" + wstring(state.AvailableForMonitor() ? L"1" : L"0") +
            L", system=" + wstring(state.AvailableForSystem() ? L"1" : L"0") + L"). Skipping hiding.");
        return { StartPlain(log), L"" };
    }
    bool alreadyHidden = state.EnabledNow();
    if (alreadyHidden)
        log(physical->DisplayLabel() + L" is already off the desktop \u2014 going straight to acquiring it.");

    std::vector<wstring> args = {
        CompositorLauncher::SpecializedArgument,
        CompositorLauncher::WaitForTargetArgument, std::to_wstring(CompositorLauncher::WaitForTargetSeconds),
        CompositorLauncher::FrameDeadlineArgument, std::to_wstring(CompositorLauncher::FrameDeadlineSeconds)
    };
    DWORD pid = CompositorLauncher::Start(args);
    log(L"Started dscomp.exe " + wstring(CompositorLauncher::SpecializedArgument) + L" " +
        CompositorLauncher::WaitForTargetArgument + L" " +
        std::to_wstring(CompositorLauncher::WaitForTargetSeconds) +
        L" (pid " + std::to_wstring(pid) + L") \u2014 it is now waiting to take over the display.");

    MonitorInfo physCopy = *physical;
    if (!alreadyHidden && !SetSpecialized(physCopy, true, request.SpecializationUi, log, ev)) {
        log(L"Display hiding was cancelled \u2014 restarting the compositor in windowed mode.");
        CompositorLauncher::StopAndWait(10000, log);
        return { StartPlain(log), L"" };
    }
    log(L"Waiting up to 15s for " + wstring(CompositorLauncher::SpecializedLiveEventName) + L"...");
    SpecializedWait wait = CompositorLauncher::WaitForSpecializedLive(pid, 15000, ev, log);
    bool keep = ApplyService::ShouldKeepHiding(wait, [&]() {
        return request.SpecializationUi == nullptr ||
               request.SpecializationUi->ConfirmKeep(physCopy, ApplyService::SpecializationConfirmSeconds, ev);
    }, log);
    if (keep) return { pid, physCopy.DeviceName };
    log(L"Rolling back display hiding...");
    CompositorLauncher::StopAndWait(10000, log);
    RestoreToDesktop(physCopy, request.SpecializationUi, log, ev);
    return { StartPlain(log), L"" };
}

std::pair<wstring, wstring> CapturePreviousPrimary(const std::vector<MonitorInfo>& before, const LogFn& log) {
    const MonitorInfo* livePrimary = nullptr;
    for (const auto& m : before) if (m.IsPrimary) { livePrimary = &m; break; }
    if (livePrimary && !livePrimary->IsDeskSplitVirtual) {
        log(L"Current primary display: " + livePrimary->DisplayLabel() + L" (" + livePrimary->DeviceName +
            L") \u2014 will be restored on Revert.");
        return { livePrimary->DeviceName, livePrimary->FriendlyName };
    }
    auto prior = SettingsStore::Load();
    if (prior && !prior->PreviousPrimaryDevice.empty()) {
        log(L"A split is already active; carrying forward the saved pre-split primary " +
            prior->PreviousPrimaryDevice + L".");
        return { prior->PreviousPrimaryDevice, prior->PreviousPrimaryFriendlyName };
    }
    const MonitorInfo* fallback = nullptr;
    for (const auto& m : before) if (!m.IsDeskSplitVirtual) { fallback = &m; break; }
    if (!fallback) {
        log(L"WARNING: could not determine a physical display to restore as primary on Revert.");
        return { L"", L"" };
    }
    log(L"No saved pre-split primary; will restore " + fallback->DeviceName + L" on Revert.");
    return { fallback->DeviceName, fallback->FriendlyName };
}

} // namespace

void ApplyService::ValidateRequest(const ApplyRequest& request) {
    if (!request.Layout)
        throw ApplyException("No layout selected.");
    if ((int)request.Resolutions.size() != request.Layout->SegmentCount())
        throw ApplyException("Layout '" + Narrow(request.Layout->Name) + "' needs " +
                             std::to_string(request.Layout->SegmentCount()) + " segment resolutions, got " +
                             std::to_string(request.Resolutions.size()) + ".");
    if (request.Layout->SegmentCount() > DESKSPLIT_MAX_MONITORS)
        throw ApplyException("Layout needs more monitors than the driver supports.");
    for (int i = 0; i < (int)request.Resolutions.size(); ++i) {
        int w = request.Resolutions[i].Width, h = request.Resolutions[i].Height;
        if (w < MinSegmentDimension || w > MaxSegmentDimension ||
            h < MinSegmentDimension || h > MaxSegmentDimension)
            throw ApplyException("Segment " + std::to_string(i + 1) + " resolution is out of range (" +
                                 std::to_string(MinSegmentDimension) + ".." + std::to_string(MaxSegmentDimension) + ").");
    }
    if (request.Physical.IsDeskSplitVirtual)
        throw ApplyException("The selected monitor is a DesktopSplitter virtual monitor; pick a physical one.");
}

void ApplyService::WriteCompositorConfig(const CompositorConfig& config) {
    try {
        if (!EnsureDirectory(ConfigDirectory()))
            throw ApplyException("Could not create " + Narrow(ConfigDirectory()) + ".");
        if (!WriteUtf8File(ConfigPath(), CompositorConfigToJson(config)))
            throw std::runtime_error("write failed");
    } catch (const std::exception& ex) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            throw ApplyException(
                "Access denied writing " + Narrow(ConfigPath()) + ".\r\n"
                "Run DesktopSplitter as administrator once \u2014 it will create " +
                Narrow(ConfigDirectory()) + " and grant the current user write access; "
                "subsequent runs do not need elevation.", ex);
        }
        throw ApplyException("Could not write " + Narrow(ConfigPath()) + ": " + ex.what(), ex);
    }
}

void ApplyService::MarkSplitInactive(bool deliberate) {
    try {
        auto settings = SettingsStore::Load();
        if (!settings) return;
        if (!settings->SplitActiveOnExit && !deliberate) return;
        settings->SplitActiveOnExit = false;
        if (deliberate) settings->ApplyOnStartup = false;
        SettingsStore::Save(*settings);
    } catch (...) {}
}

bool ApplyService::ShouldAutoApplyOnStartup(const AppSettings* settings, bool splitAlreadyActive) {
    if (!settings) return false;
    if (splitAlreadyActive) return false;
    if (settings->PhysicalDevice.empty() || settings->Segments.empty()) return false;
    return settings->ApplyOnStartup || settings->SplitActiveOnExit;
}

AppliedSnapshot ApplyService::CaptureAppliedSnapshot() {
    auto settings = SettingsStore::Load();
    if (!settings || settings->PhysicalDevice.empty() || settings->Segments.empty())
        return AppliedSnapshot::None();
    if (!HidingStatus::IsSplitActive())
        return AppliedSnapshot::None();
    AppliedSnapshot s;
    s.WasApplied = true;
    s.PhysicalDevice = settings->PhysicalDevice;
    s.Layout = settings->Layout;
    for (const auto& z : settings->Segments) s.Resolutions.push_back({ z.Width, z.Height });
    s.HidePhysicalDisplay = settings->HidePhysicalDisplay;
    return s;
}

bool ApplyService::ShouldKeepHiding(SpecializedWait wait, const std::function<bool()>& askUser, const LogFn& log) {
    switch (wait) {
    case SpecializedWait::Live:
        log(L"Compositor confirmed specialized presentation \u2014 asking you to confirm.");
        {
            bool keep = askUser();
            log(keep ? L"Keeping the hidden display." : L"Restoring the display to the desktop.");
            return keep;
        }
    case SpecializedWait::CompositorExited:
        log(L"The compositor stopped before it could present through the hidden display \u2014 rolling back without prompting.");
        return false;
    default:
        log(L"No " + wstring(CompositorLauncher::SpecializedLiveEventName) +
            L" signal within 15s \u2014 treating hiding as failed and rolling back without prompting.");
        return false;
    }
}

bool ApplyService::SwitchPrimaryTo(const wstring& targetDevice, const wstring& description, const LogFn& log) {
    return SwitchPrimary(targetDevice, description, log);
}

void ApplyService::ClearSpecializationIfOurs(const LogFn& log, ISpecializationUi* ui, HANDLE ev) {
    auto settings = SettingsStore::Load();
    wstring device = settings ? settings->SpecializedDevice : L"";
    if (device.empty()) return;
    auto all = SafeEnumerate();
    const MonitorInfo* monitor = FindLive(all, device);
    if (!monitor) {
        log(L"Note: " + device + L" was hidden by DesktopSplitter but is no longer connected.");
    } else {
        SpecializationState state; int rc = 0;
        if (MonitorSpecialization::TryGet(*monitor, state, rc) && state.EnabledNow()) {
            log(L"Restoring " + monitor->DisplayLabel() + L" to the desktop...");
            RestoreToDesktop(*monitor, ui, log, ev);
        }
    }
    if (settings) {
        settings->SpecializedDevice.clear();
        try { SettingsStore::Save(*settings); } catch (...) {}
    }
}

void ApplyService::RecoverStaleSpecialization(const LogFn& log) {
    auto settings = SettingsStore::Load();
    if (!settings || settings->SpecializedDevice.empty()) return;
    if (HidingStatus::IsSplitActive()) return;
    log(L"Recovering from a previous session: " + settings->SpecializedDevice + L" was left hidden.");
    ClearSpecializationIfOurs(log);
}

void ApplyService::RevertCore(const LogFn& log, HANDLE ev, ISpecializationUi* ui) {
    log(L"Stopping the compositor...");
    bool stopped = CompositorLauncher::StopAndWait(10000, log);
    log(stopped ? L"Compositor stopped." : L"Compositor may still be running.");
    ClearSpecializationIfOurs(log, ui, ev);
    RestorePrimaryBeforeTeardown(log);
    log(L"Sending IOCTL_DESKSPLIT_SET_CONFIG with MonitorCount = 0...");
    try {
        DriverClient::ClearConfig();
    } catch (const DriverNotFoundException& ex) {
        throw ApplyException(ex.what(), ex);
    } catch (const DriverIoException& ex) {
        throw ApplyException(ex.what(), ex);
    }
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && !Cancelled(ev)) {
        bool any = false;
        for (const auto& m : SafeEnumerate()) if (m.IsDeskSplitVirtual) { any = true; break; }
        if (!any) break;
        SleepPoll(400, ev);
    }
    MarkSplitInactive(true);
    log(L"Reverted \u2014 virtual monitors removed.");
}

void ApplyService::Revert(const LogFn& log, HANDLE ev, ISpecializationUi* ui) {
    std::lock_guard<std::mutex> lock(DisplayOperationGate);
    RevertCore(log, ev, ui);
}

static string RollBackTo(const AppliedSnapshot& snapshot, const LogFn& log, HANDLE ev, ISpecializationUi* ui);

ApplyResult ApplyService::ApplyCore(const ApplyRequest& request, const LogFn& log, HANDLE ev, AppliedSnapshot& snapshot) {
    ValidateRequest(request);
    const MonitorInfo& physical = request.Physical;
    const LayoutDefinition& layout = *request.Layout;
    int segmentCount = layout.SegmentCount();
    snapshot = CaptureAppliedSnapshot();

    if (CompositorLauncher::IsRunning()) {
        log(L"Stopping the running compositor first...");
        CompositorLauncher::StopAndWait(10000, log);
    }

    auto before = DisplayConfig::Enumerate();
    std::vector<wstring> baseline;
    for (const auto& m : before)
        if (!m.IsDeskSplitVirtual) baseline.push_back(m.DeviceName);

    auto prevPrimary = CapturePreviousPrimary(before, log);

    DESKSPLIT_CONFIG cfg = DriverClient::NewConfig();
    cfg.MonitorCount = (uint32_t)segmentCount;
    for (int i = 0; i < segmentCount; ++i) {
        cfg.Monitors[i].ModeCount = 1;
        cfg.Monitors[i].PreferredModeIndex = 0;
        cfg.Monitors[i].Modes[0].Width = (uint32_t)request.Resolutions[i].Width;
        cfg.Monitors[i].Modes[0].Height = (uint32_t)request.Resolutions[i].Height;
        cfg.Monitors[i].Modes[0].RefreshMillihertz = physical.MaxRefreshMillihertz;
    }
    wchar_t ioctl[160];
    swprintf_s(ioctl, L"Sending IOCTL_DESKSPLIT_SET_CONFIG (0x%08X) for %d monitor(s) at %u mHz...",
               IOCTL_DESKSPLIT_SET_CONFIG, segmentCount, physical.MaxRefreshMillihertz);
    log(ioctl);
    try {
        DriverClient::SetConfig(cfg);
    } catch (const DriverNotFoundException& ex) { throw ApplyException(ex.what(), ex); }
    catch (const DriverIoException& ex) { throw ApplyException(ex.what(), ex); }

    log(L"Waiting for " + std::to_wstring(segmentCount) + L" virtual monitor(s) to appear (up to 15s)...");
    auto arrived = WaitForVirtualMonitors(baseline, segmentCount, ev);
    for (const auto& m : arrived) {
        wstring extra = m.Edid ? (L"  [" + m.Edid->ToString() + L"]") : L"  [no EDID]";
        log(L"  Found " + m.DeviceName + L" (" + m.FriendlyName + L")" + extra);
    }
    auto virtualMonitors = MapSegmentsToMonitors(baseline, arrived, segmentCount, log, ev);

    log(L"Arranging virtual monitors in the virtual desktop...");
    auto current = DisplayConfig::Enumerate();
    const MonitorInfo* physicalNow = FindLive(current, physical.DeviceName);
    if (!physicalNow)
        throw ApplyException(Narrow(physical.DeviceName) + " is no longer active, so the split cannot be placed next to it.");
    auto placements = DisplayArranger::ComputePlacements(*physicalNow, layout, virtualMonitors, request.Resolutions);
    wstring appliedPrimaryDevice = virtualMonitors[0].DeviceName;
    DisplayArranger::ApplyLayout(
        DisplayArranger::ComposeLayout(current, placements, physical.MaxRefreshHz, appliedPrimaryDevice), log);

    auto refreshedList = SafeEnumerate();
    for (auto& v : virtualMonitors) {
        if (auto* cur = FindLiveMut(refreshedList, v.DeviceName)) v = *cur;
    }

    auto physRects = LayoutGeometry::ComputePhysRects(layout, physical.CurrentWidth, physical.CurrentHeight, request.Resolutions);
    wstring rectsLine = L"Segment rects on the physical monitor: ";
    for (int i = 0; i < (int)physRects.size(); ++i) {
        if (i) rectsLine += L"  ";
        rectsLine += L"[" + std::to_wstring(i + 1) + L"] " + physRects[i].ToString();
    }
    log(rectsLine);

    CompositorConfig compositorConfig;
    compositorConfig.PhysicalDevice = physical.DeviceName;
    compositorConfig.RefreshMillihertz = physical.MaxRefreshMillihertz;
    for (int i = 0; i < segmentCount; ++i) {
        CompositorSegment seg;
        seg.VirtualDevice = virtualMonitors[i].DeviceName;
        seg.Width = request.Resolutions[i].Width;
        seg.Height = request.Resolutions[i].Height;
        seg.PhysRect = CompositorRect::From(physRects[i]);
        compositorConfig.Segments.push_back(seg);
    }
    WriteCompositorConfig(compositorConfig);
    log(L"Wrote " + ConfigPath());

    auto launched = LaunchCompositor(request, log, ev);

    auto previousSettings = SettingsStore::Load();
    AppSettings saved;
    saved.PhysicalDevice = physical.DeviceName;
    saved.PhysicalFriendlyName = physical.FriendlyName;
    saved.Layout = layout.Kind;
    for (const auto& r : request.Resolutions) saved.Segments.push_back({ r.Width, r.Height });
    saved.PreviousPrimaryDevice = prevPrimary.first;
    saved.PreviousPrimaryFriendlyName = prevPrimary.second;
    saved.AppliedPrimaryDevice = appliedPrimaryDevice;
    saved.HidePhysicalDisplay = request.HidePhysicalDisplay;
    saved.SpecializedDevice = launched.second;
    saved.LastAppliedUtc = IsoNowUtc();
    saved.SplitActiveOnExit = true;
    if (previousSettings) {
        saved.ApplyOnStartup = previousSettings->ApplyOnStartup;
        saved.StartWithWindows = previousSettings->StartWithWindows;
        saved.MinimizeToTrayOnClose = previousSettings->MinimizeToTrayOnClose;
        saved.SpecializationHashes = previousSettings->SpecializationHashes;
        saved.SpecializationHashRecipe = previousSettings->SpecializationHashRecipe;
    }
    SettingsStore::Save(saved);
    log(L"Saved " + SettingsStore::FilePath());

    ApplyResult result;
    result.VirtualMonitors = virtualMonitors;
    result.ConfigPath = ConfigPath();
    result.CompositorPid = launched.first;
    return result;
}

static string RollBackToImpl(const AppliedSnapshot& snapshot, const LogFn& log, HANDLE ev, ISpecializationUi* ui) {
    if (!snapshot.WasApplied) {
        log(L"Nothing was applied before this change \u2014 reverting completely.");
        ApplyService::Revert(log, ev, ui);
        return "no split (fully reverted)";
    }
    auto all = SafeEnumerate();
    const MonitorInfo* physical = FindLive(all, snapshot.PhysicalDevice);
    const LayoutDefinition& layout = LayoutDefinition::FromKind(snapshot.Layout);
    if (!physical || (int)snapshot.Resolutions.size() != layout.SegmentCount()) {
        log(L"The previous configuration is no longer valid \u2014 reverting completely.");
        ApplyService::Revert(log, ev, ui);
        return "no split (fully reverted)";
    }
    log(L"Restoring the previous configuration: " + layout.Name + L" on " + physical->DisplayLabel() + L".");
    ApplyRequest req;
    req.Physical = *physical;
    req.Layout = &layout;
    req.Resolutions = snapshot.Resolutions;
    req.HidePhysicalDisplay = snapshot.HidePhysicalDisplay;
    req.SpecializationUi = ui;
    req.PromptToConfirm = false;
    ApplyService::Apply(req, log, ev);
    return Narrow(layout.Name + L" on " + physical->DisplayLabel());
}

ApplyResult ApplyService::Apply(const ApplyRequest& request, const LogFn& log, HANDLE ev) {
    AppliedSnapshot snapshot;
    ApplyResult applied;
    {
        std::lock_guard<std::mutex> lock(DisplayOperationGate);
        applied = ApplyCore(request, log, ev, snapshot);
    }
    if (!request.PromptToConfirm || !request.Confirmer) return applied;
    wstring sizes;
    for (size_t i = 0; i < request.Resolutions.size(); ++i) {
        if (i) sizes += L", ";
        sizes += std::to_wstring(request.Resolutions[i].Width) + L"x" + std::to_wstring(request.Resolutions[i].Height);
    }
    wstring summary = request.Layout->Name + L" on " + request.Physical.DisplayLabel() + L" (" + sizes + L")";
    if (request.Confirmer->ConfirmApply(summary, ConfirmChangesSeconds, ev)) {
        log(L"Change kept.");
        return applied;
    }
    log(L"Change not confirmed \u2014 rolling back.");
    string restored = RollBackToImpl(snapshot, log, ev, request.SpecializationUi);
    applied.RolledBack = true;
    applied.RollbackDescription = Widen(restored);
    return applied;
}

} // namespace ds
