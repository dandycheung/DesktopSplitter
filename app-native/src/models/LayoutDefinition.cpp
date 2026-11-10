#include "models/LayoutDefinition.h"

namespace ds {

static constexpr double kPreviewGap = 3;

void LayoutDefinition::BuildPreview() {
    PreviewBoxes.clear();
    PreviewBoxes.reserve(Cells.size());
    for (int i = 0; i < (int)Cells.size(); ++i) {
        const LayoutCell& c = Cells[i];
        double x = c.Fx * PreviewWidth;
        double y = c.Fy * PreviewHeight;
        double w = c.Fw * PreviewWidth;
        double h = c.Fh * PreviewHeight;
        PreviewBoxes.push_back({
            x + kPreviewGap / 2,
            y + kPreviewGap / 2,
            (std::max)(1.0, w - kPreviewGap),
            (std::max)(1.0, h - kPreviewGap),
            i
        });
    }
}

std::vector<PixelRect> LayoutDefinition::ComputePhysRectsEqual(int physWidth, int physHeight) const {
    std::vector<PixelRect> rects;
    rects.reserve(Cells.size());
    for (const LayoutCell& c : Cells) {
        int x0 = (int)std::lround(c.Fx * physWidth);
        int y0 = (int)std::lround(c.Fy * physHeight);
        int x1 = (int)std::lround((c.Fx + c.Fw) * physWidth);
        int y1 = (int)std::lround((c.Fy + c.Fh) * physHeight);
        rects.push_back({ x0, y0, (std::max)(1, x1 - x0), (std::max)(1, y1 - y0) });
    }
    return rects;
}

std::vector<SizeWH> LayoutDefinition::ComputeDefaultSegmentSizes(int physWidth, int physHeight) const {
    if (HasCustomDefaults && Kind == LayoutKind::SixteenNineCenter)
        return SixteenNineCenterDefaults(physWidth, physHeight);
    auto rects = ComputePhysRectsEqual(physWidth, physHeight);
    std::vector<SizeWH> sizes;
    sizes.reserve(rects.size());
    for (const auto& r : rects) sizes.push_back({ r.W, r.H });
    return sizes;
}

std::vector<int> LayoutDefinition::SegmentsLeftOf(int line) const {
    std::vector<int> out;
    for (int i = 0; i < (int)Cells.size(); ++i)
        if (Cells[i].LastCol() == line) out.push_back(i);
    return out;
}
std::vector<int> LayoutDefinition::SegmentsRightOf(int line) const {
    std::vector<int> out;
    for (int i = 0; i < (int)Cells.size(); ++i)
        if (Cells[i].Col == line + 1) out.push_back(i);
    return out;
}
std::vector<int> LayoutDefinition::SegmentsAbove(int line) const {
    std::vector<int> out;
    for (int i = 0; i < (int)Cells.size(); ++i)
        if (Cells[i].LastRow() == line) out.push_back(i);
    return out;
}
std::vector<int> LayoutDefinition::SegmentsBelow(int line) const {
    std::vector<int> out;
    for (int i = 0; i < (int)Cells.size(); ++i)
        if (Cells[i].Row == line + 1) out.push_back(i);
    return out;
}

std::vector<SizeWH> LayoutDefinition::SixteenNineCenterDefaults(int monitorWidth, int monitorHeight) {
    int min = LayoutGeometry::MinSegmentDimension;
    int ideal = (int)std::lround(monitorHeight * 16.0 / 9.0);
    int widest = monitorWidth - 2 * min;
    int center = Clampi((std::min)(ideal, widest), min, LayoutGeometry::MaxSegmentDimension);
    int sides = (std::max)(2, monitorWidth - center);
    int right = sides / 2;
    int left = sides - right;
    return {
        { (std::max)(1, center), (std::max)(1, monitorHeight) },
        { (std::max)(1, left),   (std::max)(1, monitorHeight) },
        { (std::max)(1, right),  (std::max)(1, monitorHeight) },
    };
}

static LayoutDefinition Make(LayoutKind kind, const wchar_t* name, const wchar_t* desc,
                             int cols, int rows,
                             std::vector<int> vlines, std::vector<int> hlines,
                             std::vector<LayoutCell> cells, bool custom = false) {
    LayoutDefinition d;
    d.Kind = kind;
    d.Name = name;
    d.Description = desc;
    d.Columns = cols;
    d.Rows = rows;
    d.VerticalSplitLines = std::move(vlines);
    d.HorizontalSplitLines = std::move(hlines);
    d.Cells = std::move(cells);
    d.HasCustomDefaults = custom;
    d.BuildPreview();
    return d;
}

const std::vector<LayoutDefinition>& LayoutDefinition::All() {
    static const std::vector<LayoutDefinition> all = {
        Make(LayoutKind::TwoVertical, L"2 \u2014 side by side",
             L"Two halves split by a vertical line", 2, 1,
             {0}, {}, {
                 {0.0, 0.0, 0.5, 1.0, 0, 0, 1, 1},
                 {0.5, 0.0, 0.5, 1.0, 1, 0, 1, 1},
             }),
        Make(LayoutKind::TwoHorizontal, L"2 \u2014 stacked",
             L"Two halves split by a horizontal line", 1, 2,
             {}, {0}, {
                 {0.0, 0.0, 1.0, 0.5, 0, 0, 1, 1},
                 {0.0, 0.5, 1.0, 0.5, 0, 1, 1, 1},
             }),
        Make(LayoutKind::ThreeColumns, L"3 \u2014 columns",
             L"Three equal columns", 3, 1,
             {0, 1}, {}, {
                 {0.0,       0.0, 1.0/3.0, 1.0, 0, 0, 1, 1},
                 {1.0/3.0,   0.0, 1.0/3.0, 1.0, 1, 0, 1, 1},
                 {2.0/3.0,   0.0, 1.0/3.0, 1.0, 2, 0, 1, 1},
             }),
        Make(LayoutKind::BigLeftTwoStackedRight, L"3 \u2014 big left + 2 right",
             L"One tall segment on the left, two stacked on the right", 2, 2,
             {0}, {0}, {
                 {0.0, 0.0, 0.5, 1.0, 0, 0, 1, 2},
                 {0.5, 0.0, 0.5, 0.5, 1, 0, 1, 1},
                 {0.5, 0.5, 0.5, 0.5, 1, 1, 1, 1},
             }),
        Make(LayoutKind::SixteenNineCenter, L"3 \u2014 16:9 centre",
             L"True 16:9 centre for gaming, side rails fill the rest", 3, 1,
             {0, 1}, {}, {
                 {0.25, 0.0, 0.50, 1.0, 1, 0, 1, 1},
                 {0.00, 0.0, 0.25, 1.0, 0, 0, 1, 1},
                 {0.75, 0.0, 0.25, 1.0, 2, 0, 1, 1},
             }, true),
        Make(LayoutKind::Grid2x2, L"4 \u2014 2x2 grid",
             L"Four quadrants", 2, 2,
             {0}, {0}, {
                 {0.0, 0.0, 0.5, 0.5, 0, 0, 1, 1},
                 {0.5, 0.0, 0.5, 0.5, 1, 0, 1, 1},
                 {0.0, 0.5, 0.5, 0.5, 0, 1, 1, 1},
                 {0.5, 0.5, 0.5, 0.5, 1, 1, 1, 1},
             }),
        Make(LayoutKind::FourColumns, L"4 \u2014 columns",
             L"Four equal columns", 4, 1,
             {0, 1, 2}, {}, {
                 {0.00, 0.0, 0.25, 1.0, 0, 0, 1, 1},
                 {0.25, 0.0, 0.25, 1.0, 1, 0, 1, 1},
                 {0.50, 0.0, 0.25, 1.0, 2, 0, 1, 1},
                 {0.75, 0.0, 0.25, 1.0, 3, 0, 1, 1},
             }),
    };
    return all;
}

const LayoutDefinition& LayoutDefinition::FromKind(LayoutKind kind) {
    for (const auto& l : All())
        if (l.Kind == kind) return l;
    return All()[0];
}

} // namespace ds
