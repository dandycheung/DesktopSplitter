#pragma once
#include "services/ApplyService.h"
#include "models/AppSettings.h"

namespace ds {

class CountdownDialog {
public:
    // heading/detail empty -> default hiding-style or apply-style is supplied by caller.
    static bool Run(HWND owner, int seconds, const wstring& heading, const wstring& detail,
                    const MonitorInfo* placeOn, bool placeOnPrimary,
                    HANDLE cancelEvent);

    // Programmatic close from the helper side (timeout / cancel).
    static void CloseActive();
};

class GuidedSettingsDialog {
public:
    // Returns true if the desired state was reached (or the user said they finished successfully).
    static bool Run(HWND owner, const MonitorInfo& target, bool wantEnabled, const wstring& reason,
                    const MonitorInfo* placeOn, HANDLE cancelEvent);
    static void OpenDisplaySettings();
};

class UiApplyConfirmer : public IApplyConfirmer {
public:
    explicit UiApplyConfirmer(HWND owner, std::function<void(const wstring&)> log);
    bool ConfirmApply(const wstring& summary, int timeoutSeconds, HANDLE cancelEvent) override;
private:
    HWND owner_;
    std::function<void(const wstring&)> log_;
};

class UiSpecializationUi : public ISpecializationUi {
public:
    explicit UiSpecializationUi(HWND owner, std::function<std::vector<MonitorInfo>()> monitors);
    bool GuideManualEnable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE cancelEvent) override;
    bool ConfirmKeep(const MonitorInfo& physical, int timeoutSeconds, HANDLE cancelEvent) override;
    void GuideManualDisable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE cancelEvent) override;
private:
    HWND owner_;
    std::function<std::vector<MonitorInfo>()> monitors_;
};

} // namespace ds
