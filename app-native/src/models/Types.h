#pragma once
#include "Common.h"

namespace ds {

struct PixelRect {
    int X = 0, Y = 0, W = 0, H = 0;
    int Right() const { return X + W; }
    int Bottom() const { return Y + H; }
    wstring ToString() const {
        wchar_t buf[80];
        swprintf_s(buf, L"%dx%d+%d+%d", W, H, X, Y);
        return buf;
    }
};

struct ZoneRect {
    double X = 0, Y = 0, W = 0, H = 0;
};

struct SizeWH {
    int Width = 0;
    int Height = 0;
};

struct PreviewBox {
    double X = 0, Y = 0, W = 0, H = 0;
    int Index = 0;
};

struct LayoutCell {
    double Fx = 0, Fy = 0, Fw = 0, Fh = 0;
    int Col = 0, Row = 0, ColSpan = 1, RowSpan = 1;
    int LastCol() const { return Col + ColSpan - 1; }
    int LastRow() const { return Row + RowSpan - 1; }
};

enum class LayoutKind {
    TwoVertical = 0,
    TwoHorizontal = 1,
    ThreeColumns = 2,
    BigLeftTwoStackedRight = 3,
    Grid2x2 = 4,
    FourColumns = 5,
    SixteenNineCenter = 6,
};

struct EdidInfo {
    wstring Manufacturer;
    uint16_t ProductCode = 0;
    uint32_t SerialNumber = 0;
    wstring ToString() const {
        wchar_t buf[80];
        swprintf_s(buf, L"%s product 0x%04X serial 0x%08X",
                   Manufacturer.c_str(), ProductCode, SerialNumber);
        return buf;
    }
};

struct MonitorInfo {
    wstring DeviceName;
    wstring FriendlyName;
    wstring AdapterPath;
    wstring MonitorDevicePath;
    wstring EdidManufacturer;
    int CurrentWidth = 0;
    int CurrentHeight = 0;
    int PositionX = 0;
    int PositionY = 0;
    int MaxWidth = 0;
    int MaxHeight = 0;
    int MaxRefreshHz = 0;
    uint32_t MaxRefreshMillihertz = 0;
    int CurrentRefreshHz = 0;
    uint32_t CurrentRefreshMillihertz = 0;
    int Index = 0;
    uint32_t AdapterIdLow = 0;
    int32_t AdapterIdHigh = 0;
    uint32_t TargetId = 0;
    bool IsPrimary = false;
    bool IsDeskSplitVirtual = false;
    std::optional<EdidInfo> Edid;
    int DeskSplitMonitorIndex = -1;

    wstring DisplayLabel() const {
        wstring n = Trim(FriendlyName);
        if (!n.empty()) return n;
        int idx = (Index == INT_MAX || Index <= 0) ? 1 : Index;
        return L"Display " + std::to_wstring(idx);
    }

    wstring Caption() const {
        wstring device = DeviceName.empty() ? L"(unknown device)" : DeviceName;
        wchar_t mode[64];
        if (CurrentWidth > 0 && CurrentHeight > 0)
            swprintf_s(mode, L"%dx%d", CurrentWidth, CurrentHeight);
        else
            wcscpy_s(mode, L"unknown size");
        wstring hz = MaxRefreshHz > 0 ? (L" @ " + std::to_wstring(MaxRefreshHz) + L"Hz") : L"";
        wstring primary = IsPrimary ? L"  (primary)" : L"";
        // em dash U+2014, matching the WPF Caption format
        return DisplayLabel() + L" \u2014 " + device + L" \u2014 " + mode + hz + primary;
    }

    wstring CurrentModeText() const {
        if (CurrentWidth <= 0 || CurrentHeight <= 0) return L"unknown";
        int hz = CurrentRefreshHz > 0 ? CurrentRefreshHz : MaxRefreshHz;
        return std::to_wstring(CurrentWidth) + L"x" + std::to_wstring(CurrentHeight) +
               L" @ " + std::to_wstring(hz) + L" Hz";
    }
};

} // namespace ds
