#pragma once
#include "models/LayoutDefinition.h"
#include "models/AppSettings.h"
#include "interop/MonitorSpecialization.h"
#include "services/CompositorLauncher.h"
#include "util/JsonIo.h"

namespace ds {

class ApplyException : public std::runtime_error {
public:
    explicit ApplyException(const string& m) : std::runtime_error(m) {}
    ApplyException(const string& m, const std::exception& inner)
        : std::runtime_error(m + " (" + inner.what() + ")") {}
};

struct ISpecializationUi {
    virtual ~ISpecializationUi() = default;
    virtual bool GuideManualEnable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE cancelEvent) = 0;
    virtual bool ConfirmKeep(const MonitorInfo& physical, int timeoutSeconds, HANDLE cancelEvent) = 0;
    virtual void GuideManualDisable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE cancelEvent) = 0;
};

struct IApplyConfirmer {
    virtual ~IApplyConfirmer() = default;
    virtual bool ConfirmApply(const wstring& summary, int timeoutSeconds, HANDLE cancelEvent) = 0;
};

struct AppliedSnapshot {
    bool WasApplied = false;
    wstring PhysicalDevice;
    LayoutKind Layout = LayoutKind::TwoVertical;
    std::vector<SizeWH> Resolutions;
    bool HidePhysicalDisplay = false;
    static AppliedSnapshot None() { return {}; }
};

struct ApplyRequest {
    MonitorInfo Physical;
    const LayoutDefinition* Layout = nullptr;
    std::vector<SizeWH> Resolutions;
    bool HidePhysicalDisplay = false;
    ISpecializationUi* SpecializationUi = nullptr;
    bool PromptToConfirm = false;
    IApplyConfirmer* Confirmer = nullptr;
};

struct ApplyResult {
    std::vector<MonitorInfo> VirtualMonitors;
    wstring ConfigPath;
    DWORD CompositorPid = 0;
    bool RolledBack = false;
    wstring RollbackDescription;
};

using LogFn = std::function<void(const wstring&)>;

class ApplyService {
public:
    static constexpr int MinSegmentDimension = LayoutGeometry::MinSegmentDimension;
    static constexpr int MaxSegmentDimension = LayoutGeometry::MaxSegmentDimension;
    static constexpr int ConfirmChangesSeconds = 15;
    static constexpr int SpecializationConfirmSeconds = 10;

    static wstring ConfigDirectory() { return GetProgramDataDir(); }
    static wstring ConfigPath() { return CompositorConfigPath(); }

    static ApplyResult Apply(const ApplyRequest& request, const LogFn& log, HANDLE cancelEvent);
    static void Revert(const LogFn& log, HANDLE cancelEvent, ISpecializationUi* ui = nullptr);
    static void MarkSplitInactive(bool deliberate);
    static bool ShouldAutoApplyOnStartup(const AppSettings* settings, bool splitAlreadyActive);
    static AppliedSnapshot CaptureAppliedSnapshot();
    static void ClearSpecializationIfOurs(const LogFn& log, ISpecializationUi* ui = nullptr, HANDLE cancelEvent = nullptr);
    static void RecoverStaleSpecialization(const LogFn& log);
    static bool SwitchPrimaryTo(const wstring& targetDevice, const wstring& description, const LogFn& log);
    static void ValidateRequest(const ApplyRequest& request);
    static void WriteCompositorConfig(const CompositorConfig& config);

    // Serialises Apply/Revert and suspend/resume primary switches.
    static std::mutex DisplayOperationGate;

    static bool ShouldKeepHiding(SpecializedWait wait, const std::function<bool()>& askUser, const LogFn& log);

private:
    static ApplyResult ApplyCore(const ApplyRequest& request, const LogFn& log, HANDLE cancelEvent, AppliedSnapshot& snapshot);
    static void RevertCore(const LogFn& log, HANDLE cancelEvent, ISpecializationUi* ui);
};

} // namespace ds

