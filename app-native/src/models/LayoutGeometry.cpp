#include "models/LayoutGeometry.h"
#include "models/LayoutDefinition.h"

namespace ds {

Tracks LayoutGeometry::ComputeTracks(const LayoutDefinition& layout, const std::vector<SizeWH>& sizes) {
    std::vector<int> cols(layout.Columns, 0);
    std::vector<int> rows(layout.Rows, 0);

    for (int i = 0; i < layout.SegmentCount() && i < (int)sizes.size(); ++i) {
        const LayoutCell& cell = layout.Cells[i];
        int w = sizes[i].Width, h = sizes[i].Height;
        if (cell.ColSpan == 1) cols[cell.Col] = (std::max)(cols[cell.Col], w);
        if (cell.RowSpan == 1) rows[cell.Row] = (std::max)(rows[cell.Row], h);
    }

    for (int i = 0; i < layout.SegmentCount() && i < (int)sizes.size(); ++i) {
        const LayoutCell& cell = layout.Cells[i];
        int w = sizes[i].Width, h = sizes[i].Height;
        if (cell.ColSpan > 1) {
            int span = 0;
            for (int c = cell.Col; c <= cell.LastCol(); ++c) span += cols[c];
            if (span < w) cols[cell.LastCol()] += w - span;
        }
        if (cell.RowSpan > 1) {
            int span = 0;
            for (int r = cell.Row; r <= cell.LastRow(); ++r) span += rows[r];
            if (span < h) rows[cell.LastRow()] += h - span;
        }
    }

    for (int& c : cols) if (c <= 0) c = MinSegmentDimension;
    for (int& r : rows) if (r <= 0) r = MinSegmentDimension;
    return { std::move(cols), std::move(rows) };
}

std::vector<int> LayoutGeometry::Normalize(const std::vector<int>& tracks, int total) {
    int n = (int)tracks.size();
    std::vector<int> result(n);
    if (n == 0) return result;
    if (n == 1) { result[0] = (std::max)(1, total); return result; }

    long long sum = 0;
    for (int t : tracks) sum += (std::max)(1, t);
    if (sum <= 0) sum = n;

    int accumulated = 0;
    for (int i = 0; i < n - 1; ++i) {
        result[i] = (std::max)(1, (int)std::lround((std::max)(1, tracks[i]) * (double)total / sum));
        accumulated += result[i];
    }
    result[n - 1] = total - accumulated;

    while (result[n - 1] < 1) {
        int largest = 0;
        for (int i = 0; i < n - 1; ++i) if (result[i] > result[largest]) largest = i;
        if (result[largest] <= 1) { result[n - 1] = 1; break; }
        result[largest]--;
        result[n - 1]++;
    }
    return result;
}

std::vector<int> LayoutGeometry::Offsets(const std::vector<int>& tracks) {
    std::vector<int> offsets(tracks.size() + 1, 0);
    for (size_t i = 0; i < tracks.size(); ++i) offsets[i + 1] = offsets[i] + tracks[i];
    return offsets;
}

std::vector<PixelRect> LayoutGeometry::ComputePhysRects(const LayoutDefinition& layout,
                                                        int monitorWidth, int monitorHeight,
                                                        const std::vector<SizeWH>& sizes) {
    Tracks tracks = ComputeTracks(layout, sizes);
    auto cols = Normalize(tracks.ColumnWidths, monitorWidth);
    auto rows = Normalize(tracks.RowHeights, monitorHeight);
    auto colOffsets = Offsets(cols);
    auto rowOffsets = Offsets(rows);

    std::vector<PixelRect> rects;
    rects.reserve(layout.Cells.size());
    for (const LayoutCell& cell : layout.Cells) {
        int x = colOffsets[cell.Col];
        int y = rowOffsets[cell.Row];
        int w = colOffsets[cell.LastCol() + 1] - x;
        int h = rowOffsets[cell.LastRow() + 1] - y;
        rects.push_back({ x, y, (std::max)(1, w), (std::max)(1, h) });
    }
    return rects;
}

std::vector<double> LayoutGeometry::FractionEdges(const std::vector<int>& tracks, double extent) {
    std::vector<double> edges(tracks.size() + 1, 0);
    double sum = 0;
    for (int t : tracks) sum += (std::max)(1, t);
    if (sum <= 0) sum = 1;
    double accumulated = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        accumulated += (std::max)(1, tracks[i]);
        edges[i + 1] = accumulated / sum * extent;
    }
    edges[tracks.size()] = extent;
    return edges;
}

std::vector<ZoneRect> LayoutGeometry::ComputeZoneRects(const LayoutDefinition& layout,
                                                       double canvasWidth, double canvasHeight,
                                                       const std::vector<SizeWH>& sizes) {
    Tracks tracks = ComputeTracks(layout, sizes);
    auto colEdges = FractionEdges(tracks.ColumnWidths, canvasWidth);
    auto rowEdges = FractionEdges(tracks.RowHeights, canvasHeight);
    std::vector<ZoneRect> zones;
    zones.reserve(layout.Cells.size());
    for (const LayoutCell& cell : layout.Cells) {
        double x = colEdges[cell.Col];
        double y = rowEdges[cell.Row];
        zones.push_back({ x, y, colEdges[cell.LastCol() + 1] - x, rowEdges[cell.LastRow() + 1] - y });
    }
    return zones;
}

int LayoutGeometry::ClampSplit(int proposedLeading, int pairTotal) {
    int lower = (std::max)(MinSegmentDimension, pairTotal - MaxSegmentDimension);
    int upper = (std::min)(MaxSegmentDimension, pairTotal - MinSegmentDimension);
    if (upper < lower) return pairTotal / 2;
    return Clampi(proposedLeading, lower, upper);
}

} // namespace ds
