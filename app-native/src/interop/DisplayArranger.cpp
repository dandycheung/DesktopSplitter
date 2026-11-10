#include "interop/DisplayArranger.h"

namespace ds {
namespace {

int SetPlacement(const MonitorPlacement& p, DWORD flags, bool useRefresh) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsExW(p.DeviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm, 0)) {
        dm = DEVMODEW{};
        dm.dmSize = sizeof(dm);
    }
    dm.dmDeviceName[0] = 0;
    dm.dmFormName[0] = 0;
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = (DWORD)p.Width;
    dm.dmPelsHeight = (DWORD)p.Height;
    dm.dmBitsPerPel = 32;
    dm.dmPosition.x = p.X;
    dm.dmPosition.y = p.Y;
    dm.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (useRefresh && p.RefreshHz > 1) {
        dm.dmDisplayFrequency = (DWORD)p.RefreshHz;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }
    return ChangeDisplaySettingsExW(p.DeviceName.c_str(), &dm, nullptr, flags, nullptr);
}

} // namespace

std::vector<VirtualPlacement> DisplayArranger::ComputePlacements(
    const MonitorInfo& physical,
    const LayoutDefinition& layout,
    const std::vector<MonitorInfo>& virtualMonitors,
    const std::vector<SizeWH>& resolutions)
{
    if ((int)virtualMonitors.size() != layout.SegmentCount())
        throw DisplayArrangeException("Expected " + std::to_string(layout.SegmentCount()) +
                                      " virtual monitors, found " + std::to_string(virtualMonitors.size()) + ".");
    if ((int)resolutions.size() != layout.SegmentCount())
        throw DisplayArrangeException("Expected " + std::to_string(layout.SegmentCount()) +
                                      " segment resolutions, got " + std::to_string(resolutions.size()) + ".");

    Tracks tracks = LayoutGeometry::ComputeTracks(layout, resolutions);
    const auto& colWidth = tracks.ColumnWidths;
    const auto& rowHeight = tracks.RowHeights;
    int baseX = physical.PositionX + physical.CurrentWidth;
    int baseY = physical.PositionY;

    std::vector<VirtualPlacement> placements;
    placements.reserve(layout.SegmentCount());
    for (int i = 0; i < layout.SegmentCount(); ++i) {
        const LayoutCell& cell = layout.Cells[i];
        int w = resolutions[i].Width, h = resolutions[i].Height;
        int x = baseX;
        for (int c = 0; c < cell.Col; ++c) x += colWidth[c];
        int y = baseY;
        for (int r = 0; r < cell.Row; ++r) y += rowHeight[r];
        placements.push_back({ virtualMonitors[i].DeviceName, w, h, x, y, cell.Col, cell.Row });
    }
    return placements;
}

std::vector<MonitorPlacement> DisplayArranger::ComposeLayout(
    const std::vector<MonitorInfo>& current,
    const std::vector<VirtualPlacement>& placements,
    int virtualRefreshHz,
    const wstring& primaryDevice)
{
    std::vector<MonitorPlacement> layout;
    for (const auto& m : current) {
        const VirtualPlacement* v = nullptr;
        for (const auto& p : placements)
            if (EqualsIgnoreCase(p.DeviceName, m.DeviceName)) { v = &p; break; }
        if (v)
            layout.push_back({ v->DeviceName, v->Width, v->Height, v->X, v->Y, virtualRefreshHz, false });
        else
            layout.push_back({ m.DeviceName, m.CurrentWidth, m.CurrentHeight, m.PositionX, m.PositionY, 0, false });
    }
    for (const auto& v : placements) {
        bool exists = false;
        for (const auto& m : current)
            if (EqualsIgnoreCase(m.DeviceName, v.DeviceName)) { exists = true; break; }
        if (!exists)
            layout.push_back({ v.DeviceName, v.Width, v.Height, v.X, v.Y, virtualRefreshHz, false });
    }
    return RebaseToPrimary(layout, primaryDevice);
}

std::vector<MonitorPlacement> DisplayArranger::RebaseToPrimary(
    const std::vector<MonitorPlacement>& placements,
    const wstring& primaryDevice)
{
    if (placements.empty())
        throw DisplayArrangeException("Cannot re-base an empty monitor set.");
    const MonitorPlacement* origin = nullptr;
    for (const auto& p : placements)
        if (EqualsIgnoreCase(p.DeviceName, primaryDevice)) { origin = &p; break; }
    if (!origin) {
        string known;
        for (const auto& p : placements) {
            if (!known.empty()) known += ", ";
            known += Narrow(p.DeviceName);
        }
        throw DisplayArrangeException(
            "Monitor '" + Narrow(primaryDevice) + "' is not in the current display set, so it cannot be made primary. Known: " + known + ".");
    }
    int dx = -origin->X, dy = -origin->Y;
    std::vector<MonitorPlacement> out;
    out.reserve(placements.size());
    for (auto p : placements) {
        p.X += dx;
        p.Y += dy;
        p.IsPrimary = EqualsIgnoreCase(p.DeviceName, primaryDevice);
        out.push_back(p);
    }
    return out;
}

void DisplayArranger::ApplyLayout(const std::vector<MonitorPlacement>& placements,
                                  const std::function<void(const wstring&)>& log)
{
    if (placements.empty()) return;
    int primaryCount = 0;
    const MonitorPlacement* primary = nullptr;
    for (const auto& p : placements) if (p.IsPrimary) { ++primaryCount; primary = &p; }
    if (primaryCount != 1)
        throw DisplayArrangeException("Exactly one monitor must be primary, got " + std::to_string(primaryCount) + ".");
    if (primary->X != 0 || primary->Y != 0)
        throw DisplayArrangeException(
            "The primary monitor must sit at (0,0); " + Narrow(primary->DeviceName) +
            " is at (" + std::to_string(primary->X) + "," + std::to_string(primary->Y) + ").");

    std::vector<wstring> problems;
    std::vector<MonitorPlacement> ordered = placements;
    std::stable_sort(ordered.begin(), ordered.end(), [](const MonitorPlacement& a, const MonitorPlacement& b) {
        return (int)b.IsPrimary < (int)a.IsPrimary ? false : a.IsPrimary && !b.IsPrimary;
    });
    // primary first: sort so IsPrimary == true comes first
    std::stable_sort(ordered.begin(), ordered.end(), [](const MonitorPlacement& a, const MonitorPlacement& b) {
        if (a.IsPrimary == b.IsPrimary) return false;
        return a.IsPrimary && !b.IsPrimary;
    });

    for (const auto& p : ordered) {
        DWORD flags = CDS_UPDATEREGISTRY | CDS_NORESET;
        if (p.IsPrimary) flags |= CDS_SET_PRIMARY;
        int rc = SetPlacement(p, flags, p.RefreshHz > 1);
        if (rc != DISP_CHANGE_SUCCESSFUL && p.RefreshHz > 1) {
            if (log) log(L"  " + p.DeviceName + L": rejected at " + std::to_wstring(p.RefreshHz) +
                         L"Hz (" + DispChangeToString(rc) + L"); retrying without an explicit refresh rate.");
            rc = SetPlacement(p, flags, false);
        }
        if (rc != DISP_CHANGE_SUCCESSFUL)
            problems.push_back(p.DeviceName + L" -> (" + std::to_wstring(p.X) + L"," +
                               std::to_wstring(p.Y) + L"): " + DispChangeToString(rc));
        else if (log)
            log(L"  " + p.DeviceName + L": " + std::to_wstring(p.Width) + L"x" + std::to_wstring(p.Height) +
                L" at (" + std::to_wstring(p.X) + L"," + std::to_wstring(p.Y) + L")" +
                (p.IsPrimary ? L"   <-- PRIMARY" : L""));
    }

    int commit = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    if (commit != DISP_CHANGE_SUCCESSFUL)
        problems.push_back(L"final apply: " + DispChangeToString(commit));

    if (!problems.empty()) {
        string msg = "Could not arrange the displays: ";
        for (size_t i = 0; i < problems.size(); ++i) {
            if (i) msg += "; ";
            msg += Narrow(problems[i]);
        }
        msg += ".";
        throw DisplayArrangeException(msg);
    }
}

std::vector<MonitorPlacement> DisplayArranger::SnapshotLayout(const std::vector<MonitorInfo>& monitors) {
    std::vector<MonitorPlacement> out;
    for (const auto& m : monitors)
        out.push_back({ m.DeviceName, m.CurrentWidth, m.CurrentHeight, m.PositionX, m.PositionY, 0, m.IsPrimary });
    return out;
}

} // namespace ds
