#pragma once
#include "Common.h"

// Colors taken verbatim from the WPF App.xaml resource dictionary.
namespace ds {
namespace theme {

constexpr COLORREF WindowBackground  = RGB(0x16, 0x19, 0x1F);
constexpr COLORREF PanelBackground   = RGB(0x1D, 0x21, 0x29);
constexpr COLORREF PanelBorder       = RGB(0x30, 0x37, 0x43);
constexpr COLORREF Foreground        = RGB(0xE6, 0xEA, 0xF2);
constexpr COLORREF Muted             = RGB(0x95, 0xA0, 0xB4);
constexpr COLORREF Accent            = RGB(0x4C, 0x8D, 0xFF);
constexpr COLORREF AccentSoft        = RGB(0x2A, 0x4A, 0x82);
constexpr COLORREF Error             = RGB(0xFF, 0x7A, 0x7A);
constexpr COLORREF SegmentFill       = RGB(0x4C, 0x8D, 0xFF);
constexpr COLORREF PreviewBackground = RGB(0x10, 0x13, 0x1A);
constexpr COLORREF CanvasBackground  = RGB(0x0C, 0x0F, 0x14);
constexpr COLORREF ZoneFill          = RGB(0x25, 0x34, 0x4F);
constexpr COLORREF ZoneStroke        = RGB(0x4C, 0x8D, 0xFF);
constexpr COLORREF ZoneNumber        = RGB(0x9D, 0xC0, 0xFF);
constexpr COLORREF Splitter          = RGB(0x5A, 0x64, 0x78);
constexpr COLORREF SplitterHover     = RGB(0x4C, 0x8D, 0xFF);
constexpr COLORREF ControlBackground = RGB(0x11, 0x14, 0x1A);
constexpr COLORREF ControlHover      = RGB(0x18, 0x1C, 0x24);
constexpr COLORREF ButtonHover       = RGB(0x27, 0x2C, 0x36);
constexpr COLORREF LayoutHover       = RGB(0x23, 0x28, 0x33);
constexpr COLORREF LayoutSelected    = RGB(0x20, 0x28, 0x36);
constexpr COLORREF ComboItemHover    = RGB(0x26, 0x2C, 0x38);
constexpr COLORREF OkGreen           = RGB(0x7F, 0xD4, 0x8A);
constexpr COLORREF WarnAmber         = RGB(0xE0, 0xB4, 0x57);

inline int Dip(int px, int dpi) { return MulDiv(px, dpi, 96); }
inline int DipRound(double px, int dpi) { return (int)std::lround(px * dpi / 96.0); }

HFONT CreateUiFont(int dpi, double dipSize, int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI");
HFONT CreateMonoFont(int dpi, double dipSize, int weight = FW_NORMAL);

} // namespace theme
} // namespace ds
