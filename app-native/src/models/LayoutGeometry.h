#pragma once
#include "models/Types.h"

namespace ds {

class LayoutDefinition;

struct Tracks {
    std::vector<int> ColumnWidths;
    std::vector<int> RowHeights;
};

class LayoutGeometry {
public:
    static constexpr int MinSegmentDimension = 640;
    static constexpr int MaxSegmentDimension = 7680;

    static Tracks ComputeTracks(const LayoutDefinition& layout, const std::vector<SizeWH>& sizes);
    static std::vector<int> Normalize(const std::vector<int>& tracks, int total);
    static std::vector<int> Offsets(const std::vector<int>& tracks);
    static std::vector<PixelRect> ComputePhysRects(const LayoutDefinition& layout,
                                                   int monitorWidth, int monitorHeight,
                                                   const std::vector<SizeWH>& sizes);
    static std::vector<ZoneRect> ComputeZoneRects(const LayoutDefinition& layout,
                                                  double canvasWidth, double canvasHeight,
                                                  const std::vector<SizeWH>& sizes);
    static int ClampSplit(int proposedLeading, int pairTotal);

private:
    static std::vector<double> FractionEdges(const std::vector<int>& tracks, double extent);
};

} // namespace ds
