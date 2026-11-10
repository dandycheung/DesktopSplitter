#include "ui/Controller.h"
#include "services/SettingsStore.h"
#include "services/StartupRegistration.h"
#include "services/CompositorLauncher.h"
#include "services/SpecializationSupport.h"
#include "interop/DriverClient.h"
#include "interop/DisplayConfig.h"
#include <thread>
#ifdef __has_include
#  if __has_include("Version.h")
#    include "Version.h"
#  endif
#endif
#ifndef DS_VERSION_WSTR
#define DS_VERSION_WSTR L"0.0.0"
#endif

namespace ds {

wstring ZoneVisual::DescribeAspect(int width, int height) {
    if (width <= 0 || height <= 0) return {};
    double actual = width / (double)height;
    struct Known { const wchar_t* label; double ratio; };
    Known known[] = {
        { L"16:9",  16.0 / 9.0 },
        { L"16:10", 16.0 / 10.0 },
        { L"21:9",  64.0 / 27.0 },
        { L"32:9",  32.0 / 9.0 },
        { L"4:3",   4.0 / 3.0 },
    };
    for (auto& k : known) {
        if (std::fabs(actual - k.ratio) / k.ratio <= 0.01) return k.label;
    }
    return {};
}

int SegmentEditor::Width() const {
    try { return std::stoi(WidthText); } catch (...) { return 0; }
}
int SegmentEditor::Height() const {
    try { return std::stoi(HeightText); } catch (...) { return 0; }
}
void SegmentEditor::Validate() {
    int w = 0, h = 0;
    bool okW = false, okH = false;
    try { size_t idx; w = std::stoi(WidthText, &idx); okW = idx == WidthText.size(); } catch (...) {}
    try { size_t idx; h = std::stoi(HeightText, &idx); okH = idx == HeightText.size(); } catch (...) {}
    if (!okW || !okH) { Error = L"Enter whole numbers."; return; }
    int lo = LayoutGeometry::MinSegmentDimension;
    int hi = LayoutGeometry::MaxSegmentDimension;
    if (w < lo || w > hi || h < lo || h > hi) {
        Error = L"Width and height must be " + std::to_wstring(lo) + L".." + std::to_wstring(hi) + L".";
        return;
    }
    Error.clear();
}
void SegmentEditor::SetTexts(const wstring& w, const wstring& h) {
    WidthText = w; HeightText = h; Validate();
}
void SegmentEditor::Set(int w, int h) {
    WidthText = std::to_wstring(w);
    HeightText = std::to_wstring(h);
    Validate();
}

wstring Controller::VersionText() {
    return wstring(L"v") + DS_VERSION_WSTR;
}

Controller::Controller() {
    LoadMonitors(false);
    {
        auto settings = SettingsStore::Load();
        RestoreSettings(settings ? &*settings : nullptr);
    }
    RefreshDriverStatus();
    RefreshCompositorStatus();
    AppendStatus(L"Ready.");
}

Controller::~Controller() {
    StopSuspensionWatcher(false);
}

const MonitorInfo* Controller::SelectedMonitor() const {
    if (selectedMonitorIndex_ < 0 || selectedMonitorIndex_ >= (int)monitors_.size()) return nullptr;
    return &monitors_[selectedMonitorIndex_];
}
const LayoutDefinition* Controller::SelectedLayout() const {
    const auto& all = Layouts();
    if (selectedLayoutIndex_ < 0 || selectedLayoutIndex_ >= (int)all.size()) return nullptr;
    return &all[selectedLayoutIndex_];
}

void Controller::NotifyChanged() {
    if (OnChanged) OnChanged();
}

void Controller::SetSelectedMonitorIndex(int i) {
    if (i == selectedMonitorIndex_) return;
    selectedMonitorIndex_ = i;
    UpdateModeTexts();
    RebuildSegments(true);
    RefreshHiding();
    NotifyChanged();
}
void Controller::SetSelectedLayoutIndex(int i) {
    if (i == selectedLayoutIndex_) return;
    selectedLayoutIndex_ = i;
    RebuildSegments(true);
    NotifyChanged();
}

void Controller::LoadPreferences() {
    StartupRegistration::RefreshIfStale([this](const wstring& s){ AppendStatus(s); });
    startWithWindows_ = StartupRegistration::IsEnabled();
    auto settings = SettingsStore::Load();
    applyOnStartup_ = settings && settings->ApplyOnStartup;
    NotifyChanged();
}

void Controller::SetStartWithWindows(bool v) {
    startWithWindows_ = v;
    StartupRegistration::Set(v, [this](const wstring& s){ AppendStatus(s); });
    PersistPreferences();
    bool actual = StartupRegistration::IsEnabled();
    if (actual != v) startWithWindows_ = actual;
    NotifyChanged();
}
void Controller::SetApplyOnStartup(bool v) {
    applyOnStartup_ = v;
    PersistPreferences();
    NotifyChanged();
}
void Controller::PersistPreferences() {
    try {
        AppSettings s = SettingsStore::Load().value_or(AppSettings{});
        s.StartWithWindows = startWithWindows_;
        s.ApplyOnStartup = applyOnStartup_;
        SettingsStore::Save(s);
    } catch (const std::exception& ex) {
        AppendStatus(L"Could not save preferences: " + Widen(ex.what()));
    }
}

void Controller::AppendStatus(const wstring& line) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf_s(stamp, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    {
        std::lock_guard<std::mutex> lock(statusLock_);
        if (!statusText_.empty()) statusText_ += L"\r\n";
        statusText_ += stamp + line;
    }
    if (OnStatusAppended) OnStatusAppended();
}

void Controller::RefreshDriverStatus() {
    try {
        wstring path;
        if (!DriverClient::TryFindDevicePath(path)) {
            driverStatus_ = DriverClient::DriverMissingMessage;
            NotifyChanged();
            return;
        }
        try {
            auto status = DriverClient::GetStatus();
            driverStatus_ = L"Driver OK (protocol v" + std::to_wstring(status.Version) + L", " +
                            std::to_wstring(status.ActiveMonitorCount) + L" virtual monitor(s)).";
        } catch (const DriverIoException&) {
            driverStatus_ = L"Driver device found (" + path + L") but GET_STATUS failed.";
        }
    } catch (const std::exception& ex) {
        driverStatus_ = L"Driver check failed: " + Widen(ex.what());
    }
    NotifyChanged();
}

void Controller::RefreshCompositorStatus() {
    if (CompositorLauncher::IsRunning())
        compositorStatus_ = L"Compositor: running.";
    else {
        wstring exe;
        if (CompositorLauncher::TryFindExecutable(exe))
            compositorStatus_ = L"Compositor: stopped (" + exe + L").";
        else
            compositorStatus_ = L"Compositor: not found \u2014 expected " + CompositorLauncher::ExpectedPath() + L".";
    }
    NotifyChanged();
}

void Controller::RefreshHiding() {
    hiding_ = HidingStatus::Query(SelectedMonitor());
    hidingDisabledReason_.clear();
    if (hiding_.SplitActive)
        hidingDisabledReason_ = L"Revert the split before changing the hiding setting.";
    NotifyChanged();
}

void Controller::UpdateModeTexts() {
    const MonitorInfo* m = SelectedMonitor();
    if (!m) {
        refreshText_ = L"\u2014";
        currentModeText_ = L"\u2014";
        return;
    }
    currentModeText_ = m->CurrentModeText();
    refreshText_ = std::to_wstring(m->MaxRefreshHz) + L" Hz  (" +
                   std::to_wstring(m->MaxRefreshMillihertz) + L" mHz)  \u2014 native " +
                   std::to_wstring(m->MaxWidth) + L"x" + std::to_wstring(m->MaxHeight);
}

void Controller::LoadMonitors(bool announce) {
    wstring previous;
    if (auto* m = SelectedMonitor()) previous = m->DeviceName;
    try {
        monitors_ = DisplayConfig::EnumeratePhysical();
        selectedMonitorIndex_ = -1;
        for (int i = 0; i < (int)monitors_.size(); ++i)
            if (EqualsIgnoreCase(monitors_[i].DeviceName, previous)) { selectedMonitorIndex_ = i; break; }
        if (selectedMonitorIndex_ < 0) {
            for (int i = 0; i < (int)monitors_.size(); ++i)
                if (monitors_[i].IsPrimary) { selectedMonitorIndex_ = i; break; }
        }
        if (selectedMonitorIndex_ < 0 && !monitors_.empty()) selectedMonitorIndex_ = 0;
        UpdateModeTexts();
        bool sameSelection = !previous.empty() && SelectedMonitor() &&
                             EqualsIgnoreCase(SelectedMonitor()->DeviceName, previous);
        bool countMatches = SelectedLayout() && (int)segments_.size() == SelectedLayout()->SegmentCount();
        suppressSegmentRebuild_ = false;
        RebuildSegments(!(sameSelection && countMatches));
        if (announce)
            AppendStatus(L"Found " + std::to_wstring(monitors_.size()) + L" physical monitor(s).");
    } catch (const std::exception& ex) {
        AppendStatus(L"Monitor enumeration failed: " + Widen(ex.what()));
    }
    NotifyChanged();
}

void Controller::OnDisplayChanged() {
    LoadMonitors(false);
    UpdateModeTexts();
    AppendStatus(L"Display configuration changed \u2014 monitor list refreshed.");
}

void Controller::ResetSegments() { RebuildSegments(true); NotifyChanged(); }

void Controller::RebuildSegments(bool resetSizes) {
    if (suppressSegmentRebuild_) return;
    const LayoutDefinition* layout = SelectedLayout();
    if (!layout) return;
    const MonitorInfo* monitor = SelectedMonitor();
    int monW = monitor ? monitor->CurrentWidth : 1920;
    int monH = monitor ? monitor->CurrentHeight : 1080;
    std::vector<SizeWH> previous;
    for (auto& s : segments_) previous.push_back({ s.Width(), s.Height() });
    bool reuse = !resetSizes && (int)previous.size() == layout->SegmentCount();
    auto defaults = layout->ComputeDefaultSegmentSizes(monW, monH);
    segments_.clear();
    for (int i = 0; i < layout->SegmentCount(); ++i) {
        SizeWH sz = reuse ? previous[i] : defaults[i];
        SegmentEditor e;
        e.Index = i;
        e.Set(sz.Width, sz.Height);
        segments_.push_back(e);
    }
    RebuildEditor();
}

void Controller::RebuildEditor() {
    const LayoutDefinition* layout = SelectedLayout();
    const MonitorInfo* monitor = SelectedMonitor();
    if (!layout || !monitor || (int)segments_.size() != layout->SegmentCount()) {
        zones_.clear();
        splitters_.clear();
        sumNote_.clear();
        return;
    }
    int monW = (std::max)(1, monitor->CurrentWidth);
    int monH = (std::max)(1, monitor->CurrentHeight);
    double scale = (std::min)(MaxCanvasWidth / monW, MaxCanvasHeight / monH);
    canvasWidth_ = (std::max)(120.0, std::round(monW * scale));
    canvasHeight_ = (std::max)(80.0, std::round(monH * scale));
    std::vector<SizeWH> sizes;
    for (auto& s : segments_) sizes.push_back({ s.Width(), s.Height() });
    auto rects = LayoutGeometry::ComputeZoneRects(*layout, canvasWidth_, canvasHeight_, sizes);
    if ((int)zones_.size() != layout->SegmentCount()) {
        zones_.clear();
        for (int i = 0; i < layout->SegmentCount(); ++i) {
            ZoneVisual z; z.Index = i; zones_.push_back(z);
        }
    }
    for (int i = 0; i < layout->SegmentCount(); ++i) {
        zones_[i].X = rects[i].X; zones_[i].Y = rects[i].Y;
        zones_[i].W = rects[i].W; zones_[i].H = rects[i].H;
        zones_[i].SizeText = std::to_wstring(sizes[i].Width) + L"x" + std::to_wstring(sizes[i].Height);
        zones_[i].AspectNote = ZoneVisual::DescribeAspect(sizes[i].Width, sizes[i].Height);
    }
    RebuildSplitters(*layout, rects);
    UpdateSumNote(*layout, sizes, monW, monH);
}

void Controller::RebuildSplitters(const LayoutDefinition& layout, const std::vector<ZoneRect>& rects) {
    int expected = (int)layout.VerticalSplitLines.size() + (int)layout.HorizontalSplitLines.size();
    if ((int)splitters_.size() != expected || !SplittersMatch(layout)) {
        splitters_.clear();
        int id = 0;
        for (int line : layout.VerticalSplitLines) splitters_.push_back({ id++, true, line });
        for (int line : layout.HorizontalSplitLines) splitters_.push_back({ id++, false, line });
    }
    for (auto& s : splitters_) {
        auto left = s.IsVertical ? layout.SegmentsLeftOf(s.Line) : layout.SegmentsAbove(s.Line);
        auto right = s.IsVertical ? layout.SegmentsRightOf(s.Line) : layout.SegmentsBelow(s.Line);
        std::vector<int> affected = left;
        affected.insert(affected.end(), right.begin(), right.end());
        if (affected.empty()) continue;
        if (s.IsVertical) {
            double x = 0;
            bool any = false;
            for (int i : left) { double e = rects[i].X + rects[i].W; if (!any || e > x) { x = e; any = true; } }
            double top = rects[affected[0]].Y, bottom = rects[affected[0]].Y + rects[affected[0]].H;
            for (int i : affected) {
                top = (std::min)(top, rects[i].Y);
                bottom = (std::max)(bottom, rects[i].Y + rects[i].H);
            }
            s.X = x - SplitterVisual::Thickness / 2;
            s.Y = top;
            s.Length = (std::max)(1.0, bottom - top);
        } else {
            double y = 0;
            bool any = false;
            for (int i : left) { double e = rects[i].Y + rects[i].H; if (!any || e > y) { y = e; any = true; } }
            double lft = rects[affected[0]].X, rgt = rects[affected[0]].X + rects[affected[0]].W;
            for (int i : affected) {
                lft = (std::min)(lft, rects[i].X);
                rgt = (std::max)(rgt, rects[i].X + rects[i].W);
            }
            s.X = lft;
            s.Y = y - SplitterVisual::Thickness / 2;
            s.Length = (std::max)(1.0, rgt - lft);
        }
    }
}

bool Controller::SplittersMatch(const LayoutDefinition& layout) const {
    int i = 0;
    for (int line : layout.VerticalSplitLines) {
        if (i >= (int)splitters_.size() || !splitters_[i].IsVertical || splitters_[i].Line != line) return false;
        ++i;
    }
    for (int line : layout.HorizontalSplitLines) {
        if (i >= (int)splitters_.size() || splitters_[i].IsVertical || splitters_[i].Line != line) return false;
        ++i;
    }
    return i == (int)splitters_.size();
}

void Controller::UpdateSumNote(const LayoutDefinition& layout, const std::vector<SizeWH>& sizes, int monW, int monH) {
    Tracks tracks = LayoutGeometry::ComputeTracks(layout, sizes);
    int sumW = 0, sumH = 0;
    for (int c : tracks.ColumnWidths) sumW += c;
    for (int r : tracks.RowHeights) sumH += r;
    wstring parts;
    if (sumW != monW) parts += L"widths sum to " + std::to_wstring(sumW) + L" px (monitor is " + std::to_wstring(monW) + L")";
    if (sumH != monH) {
        if (!parts.empty()) parts += L"; ";
        parts += L"heights sum to " + std::to_wstring(sumH) + L" px (monitor is " + std::to_wstring(monH) + L")";
    }
    sumNote_ = parts.empty() ? L"" : parts + L" \u2014 segments will be scaled into their on-screen rects.";
}

void Controller::WriteTracks(const LayoutDefinition& layout, const int* cols, const int* rows) {
    editorSuspended_ = true;
    for (int i = 0; i < layout.SegmentCount() && i < (int)segments_.size(); ++i) {
        const LayoutCell& cell = layout.Cells[i];
        int w = segments_[i].Width();
        int h = segments_[i].Height();
        if (cols) {
            int sum = 0;
            for (int c = cell.Col; c <= cell.LastCol(); ++c) sum += cols[c];
            w = (std::max)(1, sum);
        }
        if (rows) {
            int sum = 0;
            for (int r = cell.Row; r <= cell.LastRow(); ++r) sum += rows[r];
            h = (std::max)(1, sum);
        }
        segments_[i].Set(w, h);
    }
    editorSuspended_ = false;
}

void Controller::BeginSplitterDrag(int splitterId, double canvasX, double canvasY) {
    const LayoutDefinition* layout = SelectedLayout();
    const MonitorInfo* monitor = SelectedMonitor();
    if (!layout || !monitor || isBusy_) return;
    const SplitterVisual* splitter = nullptr;
    for (auto& s : splitters_) if (s.Id == splitterId) { splitter = &s; break; }
    if (!splitter) return;
    std::vector<SizeWH> sizes;
    for (auto& s : segments_) sizes.push_back({ s.Width(), s.Height() });
    Tracks tracks = LayoutGeometry::ComputeTracks(*layout, sizes);
    int monW = (std::max)(1, monitor->CurrentWidth);
    int monH = (std::max)(1, monitor->CurrentHeight);
    auto cols = LayoutGeometry::Normalize(tracks.ColumnWidths, monW);
    auto rows = LayoutGeometry::Normalize(tracks.RowHeights, monH);
    WriteTracks(*layout, cols.data(), rows.data());
    dragSplitter_ = splitter;
    dragTracks_ = splitter->IsVertical ? cols : rows;
    RebuildEditor();
    double linePos = splitter->IsVertical
        ? splitter->X + SplitterVisual::Thickness / 2
        : splitter->Y + SplitterVisual::Thickness / 2;
    dragGrabOffset_ = (splitter->IsVertical ? canvasX : canvasY) - linePos;
}

void Controller::UpdateSplitterDrag(double canvasX, double canvasY) {
    const SplitterVisual* splitter = dragSplitter_;
    const LayoutDefinition* layout = SelectedLayout();
    const MonitorInfo* monitor = SelectedMonitor();
    if (!splitter || !layout || !monitor) return;
    if ((int)dragTracks_.size() < splitter->Line + 2) return;
    double canvasExtent = splitter->IsVertical ? canvasWidth_ : canvasHeight_;
    int monitorExtent = splitter->IsVertical ? (std::max)(1, monitor->CurrentWidth)
                                             : (std::max)(1, monitor->CurrentHeight);
    if (canvasExtent <= 0) return;
    double targetCanvas = (splitter->IsVertical ? canvasX : canvasY) - dragGrabOffset_;
    double targetMonitor = targetCanvas * (monitorExtent / canvasExtent);
    int before = 0;
    for (int i = 0; i < splitter->Line; ++i) before += dragTracks_[i];
    int pairTotal = dragTracks_[splitter->Line] + dragTracks_[splitter->Line + 1];
    int leading = LayoutGeometry::ClampSplit((int)std::lround(targetMonitor - before), pairTotal);
    int trailing = pairTotal - leading;
    auto updated = dragTracks_;
    updated[splitter->Line] = leading;
    updated[splitter->Line + 1] = trailing;
    if (splitter->IsVertical) WriteTracks(*layout, updated.data(), nullptr);
    else WriteTracks(*layout, nullptr, updated.data());
    dragTracks_ = updated;
    RebuildEditor();
}

void Controller::EndSplitterDrag() {
    if (!dragSplitter_) return;
    dragSplitter_ = nullptr;
    dragTracks_.clear();
    RebuildEditor();
}

bool Controller::CanApply() const {
    if (isBusy_ || !SelectedMonitor() || !SelectedLayout()) return false;
    if ((int)segments_.size() != SelectedLayout()->SegmentCount()) return false;
    for (const auto& s : segments_) if (!s.IsValid()) return false;
    return true;
}

bool Controller::RestoreSettings(const AppSettings* settings) {
    if (!settings) return false;
    int mon = -1;
    for (int i = 0; i < (int)monitors_.size(); ++i)
        if (EqualsIgnoreCase(monitors_[i].DeviceName, settings->PhysicalDevice)) { mon = i; break; }
    if (mon < 0) {
        for (int i = 0; i < (int)monitors_.size(); ++i)
            if (EqualsIgnoreCase(monitors_[i].FriendlyName, settings->PhysicalFriendlyName)) { mon = i; break; }
    }
    if (mon < 0) return false;
    suppressSegmentRebuild_ = true;
    selectedMonitorIndex_ = mon;
    const auto& all = Layouts();
    selectedLayoutIndex_ = 0;
    for (int i = 0; i < (int)all.size(); ++i)
        if (all[i].Kind == settings->Layout) { selectedLayoutIndex_ = i; break; }
    suppressSegmentRebuild_ = false;
    UpdateModeTexts();
    RebuildSegments(true);
    if ((int)settings->Segments.size() == (int)segments_.size()) {
        editorSuspended_ = true;
        for (int i = 0; i < (int)segments_.size(); ++i)
            segments_[i].Set(settings->Segments[i].Width, settings->Segments[i].Height);
        editorSuspended_ = false;
        RebuildEditor();
        AppendStatus(L"Restored last settings from " + SettingsStore::FilePath() + L".");
    }
    NotifyChanged();
    return true;
}

void Controller::StartSuspensionWatcher() {
    auto settings = SettingsStore::Load();
    if (!settings) return;
    wstring active = settings->AppliedPrimaryDevice;
    wstring physical = settings->PreviousPrimaryDevice.empty() ? settings->PhysicalDevice
                                                               : settings->PreviousPrimaryDevice;
    if (!suspensionWatcher_)
        suspensionWatcher_ = std::make_unique<SplitSuspensionWatcher>([this](const wstring& s){ AppendStatus(s); });
    suspensionWatcher_->Start(active, physical);
}

void Controller::StopSuspensionWatcher(bool restorePrimary) {
    if (suspensionWatcher_) suspensionWatcher_->Stop(restorePrimary);
}

void Controller::RecoverThenRefresh() {
    try {
        SpecializationHashStore::SeedKnownHashes(monitors_, [this](const wstring& s){ AppendStatus(s); });
        ApplyService::RecoverStaleSpecialization([this](const wstring& s){ AppendStatus(s); });
        RefreshHiding();
    } catch (const std::exception& ex) {
        AppendStatus(L"Display-hiding recovery check failed: " + Widen(ex.what()));
    }
}

void Controller::AutoApplyOnStartup() {
    auto settingsOpt = SettingsStore::Load();
    const AppSettings* settings = settingsOpt ? &*settingsOpt : nullptr;
    bool splitActive = HidingStatus::IsSplitActive();
    if (!ApplyService::ShouldAutoApplyOnStartup(settings, splitActive)) return;
    if (!RestoreSettings(settings)) {
        AppendStatus(L"Skipping auto-apply: the saved monitor is not connected.");
        return;
    }
    AppendStatus(L"--- Re-applying the saved configuration (no confirmation at startup) ---");
    ApplyAsync(false);
}

void Controller::RevertOnExit() {
    try {
        StopSuspensionWatcher(false);
        if (!HidingStatus::IsSplitActive()) {
            ApplyService::MarkSplitInactive(false);
            return;
        }
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        // 20s cap
        HANDLE timerThread = nullptr;
        // Use a waitable timer via a simple timeout event
        // We'll just call Revert; the service polls and should finish quickly.
        // Time-bounded by waiting on a worker? For exit, call directly.
        UNREFERENCED_PARAMETER(timerThread);
        try {
            ApplyService::Revert([](const wstring&){}, ev);
        } catch (...) {}
        if (ev) CloseHandle(ev);
        auto settings = SettingsStore::Load();
        if (settings) {
            settings->SplitActiveOnExit = true;
            SettingsStore::Save(*settings);
        }
    } catch (...) {}
}

void Controller::EnableHiding() {
    if (hiding_.SplitActive) { RefreshHiding(); return; }
    auto result = HidingStatus::WriteIntent(true);
    AppendStatus(result.Message);
    RefreshHiding();
}
void Controller::DisableHiding() {
    if (hiding_.SplitActive) { RefreshHiding(); return; }
    auto result = HidingStatus::WriteIntent(false);
    AppendStatus(result.Message);
    RefreshHiding();
}

void Controller::ApplyAsync(bool promptToConfirm) {
    if (!CanApply()) return;
    isBusy_ = true;
    NotifyChanged();
    MonitorInfo physical = *SelectedMonitor();
    const LayoutDefinition* layout = SelectedLayout();
    std::vector<SizeWH> resolutions;
    for (auto& s : segments_) resolutions.push_back({ s.Width(), s.Height() });
    bool hide = hiding_.IntentOn;
    IApplyConfirmer* confirmer = confirmer_;
    ISpecializationUi* ui = specializationUi_;
    AppendStatus(L"--- Applying '" + layout->Name + L"' to " + physical.DisplayLabel() +
                 L" (" + physical.DeviceName + L") ---");
    HWND hwnd = UiThreadHwnd;
    std::thread([this, physical, layout, resolutions, hide, confirmer, ui, promptToConfirm, hwnd]() {
        wstring err;
        ApplyResult result;
        bool ok = false;
        try {
            ApplyRequest req;
            req.Physical = physical;
            req.Layout = layout;
            req.Resolutions = resolutions;
            req.HidePhysicalDisplay = hide;
            req.SpecializationUi = ui;
            req.PromptToConfirm = promptToConfirm;
            req.Confirmer = confirmer;
            result = ApplyService::Apply(req, [this](const wstring& s){ AppendStatus(s); }, nullptr);
            ok = true;
            if (result.RolledBack)
                AppendStatus(L"Change reverted \u2014 restored " + result.RollbackDescription + L".");
            else
                AppendStatus(L"Done. " + std::to_wstring(result.VirtualMonitors.size()) + L" virtual monitor(s) active.");
            if (!result.RolledBack) StartSuspensionWatcher();
        } catch (const std::exception& ex) {
            err = Widen(ex.what());
            AppendStatus(L"Apply failed: " + err);
        }
        isBusy_ = false;
        if (hwnd) PostMessageW(hwnd, WM_DS_WORK_DONE, 0, 0);
    }).detach();
}

void Controller::RevertAsync() {
    isBusy_ = true;
    NotifyChanged();
    AppendStatus(L"--- Reverting ---");
    StopSuspensionWatcher(false);
    HWND hwnd = UiThreadHwnd;
    ISpecializationUi* ui = specializationUi_;
    std::thread([this, hwnd, ui]() {
        try {
            ApplyService::Revert([this](const wstring& s){ AppendStatus(s); }, nullptr, ui);
        } catch (const std::exception& ex) {
            AppendStatus(L"Revert failed: " + Widen(ex.what()));
        }
        isBusy_ = false;
        if (hwnd) PostMessageW(hwnd, WM_DS_WORK_DONE, 0, 0);
    }).detach();
}

void Controller::ApplyLastAsync() {
    auto settings = SettingsStore::Load();
    if (!settings || settings->Segments.empty()) {
        AppendStatus(L"No previously applied configuration found.");
        return;
    }
    if (!RestoreSettings(&*settings)) {
        AppendStatus(L"Monitor '" + settings->PhysicalDevice + L"' from the saved settings is not connected.");
        return;
    }
    ApplyAsync(true);
}

} // namespace ds
