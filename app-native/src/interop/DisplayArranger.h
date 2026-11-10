#pragma once
#include "models/LayoutDefinition.h"
#include "interop/DisplayConfig.h"

namespace ds {

class DisplayArrangeException : public std::runtime_error {
public:
    explicit DisplayArrangeException(const string& m) : std::runtime_error(m) {}
};

struct VirtualPlacement {
    wstring DeviceName;
    int Width = 0, Height = 0, X = 0, Y = 0, Col = 0, Row = 0;
};

struct MonitorPlacement {
    wstring DeviceName;
    int Width = 0, Height = 0, X = 0, Y = 0, RefreshHz = 0;
    bool IsPrimary = false;
    int Right() const { return X + Width; }
    int Bottom() const { return Y + Height; }
};

class DisplayArranger {
public:
    static std::vector<VirtualPlacement> ComputePlacements(
        const MonitorInfo& physical,
        const LayoutDefinition& layout,
        const std::vector<MonitorInfo>& virtualMonitors,
        const std::vector<SizeWH>& resolutions);

    static std::vector<MonitorPlacement> ComposeLayout(
        const std::vector<MonitorInfo>& current,
        const std::vector<VirtualPlacement>& placements,
        int virtualRefreshHz,
        const wstring& primaryDevice);

    static std::vector<MonitorPlacement> RebaseToPrimary(
        const std::vector<MonitorPlacement>& placements,
        const wstring& primaryDevice);

    static void ApplyLayout(const std::vector<MonitorPlacement>& placements,
                            const std::function<void(const wstring&)>& log = {});

    static std::vector<MonitorPlacement> SnapshotLayout(const std::vector<MonitorInfo>& monitors);
};

} // namespace ds
