#pragma once
#include "models/LayoutDefinition.h"
#include "models/AppSettings.h"
#include "services/ApplyService.h"
#include "services/HidingStatus.h"
#include "services/SplitSuspensionWatcher.h"

namespace ds {

struct ZoneVisual {
    int Index = 0;
    int Number() const { return Index + 1; }
    double X = 0, Y = 0, W = 0, H = 0;
    wstring SizeText;
    wstring AspectNote;
    bool HasAspectNote() const { return !AspectNote.empty(); }
    static wstring DescribeAspect(int width, int height);
};

struct SplitterVisual {
    static constexpr double Thickness = 7;
    int Id = 0;
    bool IsVertical = true;
    int Line = 0;
    double X = 0, Y = 0, Length = 0;
    double W() const { return IsVertical ? Thickness : Length; }
    double H() const { return IsVertical ? Length : Thickness; }
};

struct SegmentEditor {
    int Index = 0;
    wstring WidthText, HeightText, Error;
    bool HasError() const { return !Error.empty(); }
    bool IsValid() const { return !HasError(); }
    int Width() const;
    int Height() const;
    wstring Title() const { return L"Zone " + std::to_wstring(Index + 1); }
    void SetTexts(const wstring& w, const wstring& h);
    void Set(int w, int h);
    void Validate();
};

class Controller {
public:
    static constexpr double MaxCanvasWidth = 660;
    static constexpr double MaxCanvasHeight = 380;

    std::function<void()> OnChanged;          // refresh UI
    std::function<void()> OnStatusAppended;   // scroll status
    std::function<void()> OnMinimizeToTray;
    IApplyConfirmer* confirmer_ = nullptr;
    ISpecializationUi* specializationUi_ = nullptr;

    Controller();
    ~Controller();

    static wstring VersionText();

    const std::vector<LayoutDefinition>& Layouts() const { return LayoutDefinition::All(); }
    const std::vector<MonitorInfo>& Monitors() const { return monitors_; }
    const std::vector<SegmentEditor>& Segments() const { return segments_; }
    std::vector<SegmentEditor>& Segments() { return segments_; }
    const std::vector<ZoneVisual>& Zones() const { return zones_; }
    const std::vector<SplitterVisual>& Splitters() const { return splitters_; }

    const MonitorInfo* SelectedMonitor() const;
    int SelectedMonitorIndex() const { return selectedMonitorIndex_; }
    void SetSelectedMonitorIndex(int i);

    int SelectedLayoutIndex() const { return selectedLayoutIndex_; }
    void SetSelectedLayoutIndex(int i);
    const LayoutDefinition* SelectedLayout() const;

    const wstring& StatusText() const { return statusText_; }
    const wstring& DriverStatus() const { return driverStatus_; }
    const wstring& CompositorStatus() const { return compositorStatus_; }
    const wstring& RefreshText() const { return refreshText_; }
    const wstring& CurrentModeText() const { return currentModeText_; }
    const wstring& SumNote() const { return sumNote_; }
    bool HasSumNote() const { return !sumNote_.empty(); }
    double CanvasWidth() const { return canvasWidth_; }
    double CanvasHeight() const { return canvasHeight_; }

    bool IsBusy() const { return isBusy_; }
    bool IsIdle() const { return !isBusy_; }
    bool CanApply() const;

    bool StartWithWindows() const { return startWithWindows_; }
    void SetStartWithWindows(bool v);
    bool ApplyOnStartup() const { return applyOnStartup_; }
    void SetApplyOnStartup(bool v);

    bool IntentOn() const { return hiding_.IntentOn; }
    const HidingSnapshot& Hiding() const { return hiding_; }
    const wstring& DisabledReason() const { return hidingDisabledReason_; }
    bool HasDisabledReason() const { return !hidingDisabledReason_.empty(); }

    void LoadPreferences();
    void LoadMonitors(bool announce);
    void OnDisplayChanged();
    void ResetSegments();
    void RebuildEditor();
    void BeginSplitterDrag(int splitterId, double canvasX, double canvasY);
    void UpdateSplitterDrag(double canvasX, double canvasY);
    void EndSplitterDrag();
    bool IsDragging() const { return dragSplitter_ != nullptr; }

    void AppendStatus(const wstring& line);
    void RefreshDriverStatus();
    void RefreshCompositorStatus();
    void RefreshHiding();

    void ApplyAsync(bool promptToConfirm = true);
    void RevertAsync();
    void ApplyLastAsync();
    void AutoApplyOnStartup();
    void RevertOnExit();
    void RecoverThenRefresh();

    void EnableHiding();
    void DisableHiding();

    HWND UiThreadHwnd = nullptr; // set by the main window for posted results

    static constexpr UINT WM_DS_LOG = WM_APP + 1;
    static constexpr UINT WM_DS_WORK_DONE = WM_APP + 2;
    static constexpr UINT WM_DS_REFRESH = WM_APP + 3;

    void NotifyChanged();

private:
    std::vector<MonitorInfo> monitors_;
    int selectedMonitorIndex_ = -1;
    int selectedLayoutIndex_ = 0;
    std::vector<SegmentEditor> segments_;
    std::vector<ZoneVisual> zones_;
    std::vector<SplitterVisual> splitters_;
    wstring statusText_, driverStatus_, compositorStatus_;
    wstring refreshText_ = L"\u2014";
    wstring currentModeText_ = L"\u2014";
    wstring sumNote_;
    double canvasWidth_ = MaxCanvasWidth;
    double canvasHeight_ = 200;
    bool isBusy_ = false;
    bool suppressSegmentRebuild_ = false;
    bool editorSuspended_ = false;
    bool startWithWindows_ = false;
    bool applyOnStartup_ = false;
    HidingSnapshot hiding_;
    wstring hidingDisabledReason_;
    std::unique_ptr<SplitSuspensionWatcher> suspensionWatcher_;
    std::mutex statusLock_;

    const SplitterVisual* dragSplitter_ = nullptr;
    std::vector<int> dragTracks_;
    double dragGrabOffset_ = 0;

    void UpdateModeTexts();
    void RebuildSegments(bool resetSizes);
    void RebuildSplitters(const LayoutDefinition& layout, const std::vector<ZoneRect>& rects);
    bool SplittersMatch(const LayoutDefinition& layout) const;
    void UpdateSumNote(const LayoutDefinition& layout, const std::vector<SizeWH>& sizes, int monW, int monH);
    void WriteTracks(const LayoutDefinition& layout, const int* cols, const int* rows);
    void RaiseBusy(bool busy);
    bool RestoreSettings(const AppSettings* settings);
    void PersistPreferences();
    void StartSuspensionWatcher();
    void StopSuspensionWatcher(bool restorePrimary);
};

} // namespace ds
