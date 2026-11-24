#include "ui/Theme.h"
#include <dwmapi.h>
#include <uxtheme.h>
#pragma warning(push)
#pragma warning(disable: 4458)
namespace Gdiplus {
using std::min;
using std::max;
}
#include <gdiplus.h>
#pragma warning(pop)

namespace ds {
namespace theme {
namespace {

ULONG_PTR g_gdipToken = 0;

Gdiplus::Color ToGdipColor(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

void BuildRoundedRectPath(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (w <= 0.0f || h <= 0.0f) return;
    float maxR = (std::min)(w, h) * 0.5f;
    if (r > maxR) r = maxR;
    if (r <= 0.01f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

HMODULE GetUxTheme() {
    static HMODULE mod = []() -> HMODULE {
        HMODULE h = ::GetModuleHandleW(L"uxtheme.dll");
        if (!h) h = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return h;
    }();
    return mod;
}

void AllowDarkModeForWindow(HWND hwnd) {
    if (!hwnd) return;
    HMODULE ux = GetUxTheme();
    if (!ux) return;
    using AllowDarkFn = BOOL(WINAPI*)(HWND, BOOL);
    static auto fn = reinterpret_cast<AllowDarkFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(133)));
    if (fn) fn(hwnd, TRUE);
}

} // namespace

HFONT CreateUiFont(int dpi, double dipSize, int weight, const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfHeight = -DipRound(dipSize, dpi);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

HFONT CreateMonoFont(int dpi, double dipSize, int weight) {
    return CreateUiFont(dpi, dipSize, weight, L"Consolas");
}

void InitAppDarkMode() {
    HMODULE ux = GetUxTheme();
    if (!ux) return;
    // Ordinal 135: SetPreferredAppMode on Win10 1903+ (ForceDark = 2),
    // or AllowDarkModeForApp(BOOL) on Win10 1809 (non-zero = TRUE).
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setPreferred = reinterpret_cast<SetPreferredAppModeFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    if (setPreferred) setPreferred(2);
    auto flushMenus = reinterpret_cast<FlushMenuThemesFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (flushMenus) flushMenus();
}

void EnableDarkTitleBar(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+); 19 is the older value.
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    ::DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    AllowDarkModeForWindow(hwnd);
}

void ApplyDarkExplorerTheme(HWND hwnd) {
    if (!hwnd) return;
    AllowDarkModeForWindow(hwnd);
    ::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

void ApplyDarkComboTheme(HWND combo) {
    if (!combo) return;
    AllowDarkModeForWindow(combo);
    ::SetWindowTheme(combo, L"DarkMode_CFD", nullptr);
    COMBOBOXINFO cbi{};
    cbi.cbSize = sizeof(cbi);
    if (::GetComboBoxInfo(combo, &cbi)) {
        if (cbi.hwndList) {
            AllowDarkModeForWindow(cbi.hwndList);
            ::SetWindowTheme(cbi.hwndList, L"DarkMode_Explorer", nullptr);
        }
        if (cbi.hwndItem) {
            AllowDarkModeForWindow(cbi.hwndItem);
            ::SetWindowTheme(cbi.hwndItem, L"DarkMode_CFD", nullptr);
        }
    }
}

void InitGraphics() {
    if (g_gdipToken) return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdipToken, &input, nullptr);
}

void ShutdownGraphics() {
    if (!g_gdipToken) return;
    Gdiplus::GdiplusShutdown(g_gdipToken);
    g_gdipToken = 0;
}

void DrawRoundRectAA(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border, int borderW) {
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);
    if (!hdc || w <= 0.0f || h <= 0.0f) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    float bw = borderW > 0 ? static_cast<float>(borderW) : 0.0f;
    float inset = bw * 0.5f;
    float pw = w - bw;
    float ph = h - bw;
    if (pw <= 0.0f || ph <= 0.0f) return;

    float r = static_cast<float>((std::max)(0, radius));
    if (r > 0.0f && inset > 0.0f) r = (std::max)(0.0f, r - inset * 0.5f);

    Gdiplus::GraphicsPath path;
    BuildRoundedRectPath(path, static_cast<float>(rc.left) + inset, static_cast<float>(rc.top) + inset, pw, ph, r);

    Gdiplus::SolidBrush fillBr(ToGdipColor(fill));
    g.FillPath(&fillBr, &path);

    if (borderW > 0) {
        Gdiplus::Pen pen(ToGdipColor(border), bw);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, &path);
    }
}

void DrawCheckmarkAA(HDC hdc, const RECT& box, int strokeW, COLORREF color) {
    float w = static_cast<float>(box.right - box.left);
    float h = static_cast<float>(box.bottom - box.top);
    if (!hdc || w <= 0.0f || h <= 0.0f) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    Gdiplus::PointF pts[3] = {
        { box.left + w * 0.22f, box.top + h * 0.52f },
        { box.left + w * 0.43f, box.top + h * 0.74f },
        { box.left + w * 0.79f, box.top + h * 0.27f }
    };
    Gdiplus::Pen pen(ToGdipColor(color), static_cast<float>((std::max)(1, strokeW)));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawLines(&pen, pts, 3);
}

void DrawDownArrowAA(HDC hdc, int cx, int cy, int width, int height, COLORREF color) {
    if (!hdc || width <= 0 || height <= 0) return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    float hw = static_cast<float>(width) * 0.5f;
    float hh = static_cast<float>(height) * 0.5f;
    float fcx = static_cast<float>(cx);
    float fcy = static_cast<float>(cy);
    Gdiplus::PointF tri[3] = {
        { fcx - hw, fcy - hh },
        { fcx + hw, fcy - hh },
        { fcx,      fcy + hh }
    };
    Gdiplus::SolidBrush br(ToGdipColor(color));
    g.FillPolygon(&br, tri, 3);
}

} // namespace theme
} // namespace ds
