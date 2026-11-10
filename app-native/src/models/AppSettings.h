#pragma once
#include "models/Types.h"

namespace ds {

struct SegmentSize {
    int Width = 0;
    int Height = 0;
};

struct CompositorRect {
    int X = 0, Y = 0, W = 0, H = 0;
    static CompositorRect From(const PixelRect& r) { return { r.X, r.Y, r.W, r.H }; }
};

struct CompositorSegment {
    wstring VirtualDevice;
    int Width = 0;
    int Height = 0;
    CompositorRect PhysRect;
};

struct CompositorConfig {
    wstring PhysicalDevice;
    uint32_t RefreshMillihertz = 0;
    std::vector<CompositorSegment> Segments;
};

struct AppSettings {
    wstring PhysicalDevice;
    wstring PhysicalFriendlyName;
    LayoutKind Layout = LayoutKind::TwoVertical;
    std::vector<SegmentSize> Segments;
    wstring PreviousPrimaryDevice;
    wstring PreviousPrimaryFriendlyName;
    wstring AppliedPrimaryDevice;
    bool HidePhysicalDisplay = false;
    wstring SpecializedDevice;
    wstring SpecializationHashRecipe;
    // keyed by monitor device interface path (case-insensitive compare at lookup)
    std::map<wstring, wstring> SpecializationHashes;
    std::optional<wstring> LastAppliedUtc; // ISO-8601 if present
    bool MinimizeToTrayOnClose = true;
    bool StartWithWindows = false;
    bool ApplyOnStartup = false;
    bool SplitActiveOnExit = false;
};

class CountdownPlacement {
public:
    static const MonitorInfo* ChooseMonitor(const std::vector<MonitorInfo>& monitors,
                                            const wstring& targetDevice);
    static PixelRect Center(const MonitorInfo& monitor, int windowWidth, int windowHeight);
};

} // namespace ds
