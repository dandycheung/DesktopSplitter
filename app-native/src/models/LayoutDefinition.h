#pragma once
#include "models/Types.h"
#include "models/LayoutGeometry.h"

namespace ds {

class LayoutDefinition {
public:
    static constexpr double PreviewWidth = 132;
    static constexpr double PreviewHeight = 76;

    LayoutKind Kind = LayoutKind::TwoVertical;
    wstring Name;
    wstring Description;
    int Columns = 1;
    int Rows = 1;
    std::vector<LayoutCell> Cells;
    std::vector<PreviewBox> PreviewBoxes;
    std::vector<int> VerticalSplitLines;
    std::vector<int> HorizontalSplitLines;
    bool HasCustomDefaults = false;

    int SegmentCount() const { return (int)Cells.size(); }

    std::vector<SizeWH> ComputeDefaultSegmentSizes(int physWidth, int physHeight) const;
    std::vector<PixelRect> ComputePhysRectsEqual(int physWidth, int physHeight) const;

    std::vector<int> SegmentsLeftOf(int line) const;
    std::vector<int> SegmentsRightOf(int line) const;
    std::vector<int> SegmentsAbove(int line) const;
    std::vector<int> SegmentsBelow(int line) const;

    static std::vector<SizeWH> SixteenNineCenterDefaults(int monitorWidth, int monitorHeight);
    static const std::vector<LayoutDefinition>& All();
    static const LayoutDefinition& FromKind(LayoutKind kind);
    void BuildPreview();
};

} // namespace ds
