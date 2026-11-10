#include "models/AppSettings.h"

namespace ds {

const MonitorInfo* CountdownPlacement::ChooseMonitor(const std::vector<MonitorInfo>& monitors,
                                                     const wstring& targetDevice) {
    if (monitors.empty()) return nullptr;

    std::vector<const MonitorInfo*> others;
    for (const auto& m : monitors) {
        if (!EqualsIgnoreCase(m.DeviceName, targetDevice)) others.push_back(&m);
    }
    if (!others.empty()) {
        for (auto* m : others) if (m->IsPrimary) return m;
        auto best = others[0];
        for (auto* m : others) if (m->Index < best->Index) best = m;
        return best;
    }
    for (const auto& m : monitors) if (m.IsPrimary) return &m;
    return &monitors[0];
}

PixelRect CountdownPlacement::Center(const MonitorInfo& monitor, int windowWidth, int windowHeight) {
    int w = (std::max)(1, windowWidth);
    int h = (std::max)(1, windowHeight);
    int x = monitor.PositionX + (monitor.CurrentWidth - w) / 2;
    int y = monitor.PositionY + (monitor.CurrentHeight - h) / 2;
    if (w >= monitor.CurrentWidth) x = monitor.PositionX;
    if (h >= monitor.CurrentHeight) y = monitor.PositionY;
    return { x, y, w, h };
}

} // namespace ds
