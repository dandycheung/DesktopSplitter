#include "ui/MainWindow.h"
#include "interop/DisplayConfig.h"
#include "ui/Theme.h"
#include "services/SettingsStore.h"
#include "services/StartupRegistration.h"
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace ds {
namespace {
constexpr UINT kTrayCallback = WM_APP + 41;
constexpr UINT kConfirmHop = WM_APP + 40;

int WindowDpi(HWND hwnd) {
    typedef UINT (WINAPI *Fn)(HWND);
    static Fn p = (Fn)GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (p && hwnd) return (int)p(hwnd);
    HDC hdc = ::GetDC(hwnd ? hwnd : nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ::ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}

void DarkTitle(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

void CenterOnWorkArea(int w, int h, int& x, int& y) {
    RECT wa; ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top - h) / 2;
}

HBRUSH Solid(COLORREF c) { return CreateSolidBrush(c); }

} // namespace

CMainWindow::CMainWindow() = default;
CMainWindow::~CMainWindow() {
    RemoveTray();
    auto delF = [](HFONT& f){ if (f) { DeleteObject(f); f = nullptr; } };
    delF(fontTitle_); delF(fontBody_); delF(fontMuted_); delF(fontHeader_);
    delF(fontSmall_); delF(fontMono_); delF(fontZone_);
    if (brWindow_) DeleteObject(brWindow_);
    if (brPanel_) DeleteObject(brPanel_);
    if (brEdit_) DeleteObject(brEdit_);
}


void CMainWindow::SetWindowClassEx(WNDCLASSEX& wce) {
    TWL::CUserWindow::SetWindowClassEx(wce);
    wce.style |= CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wce.hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
    wce.hIconSm = wce.hIcon;
    wce.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
}

void CMainWindow::ProcessDpiChangedMessage(WPARAM wParam, LPARAM) {
    dpi_ = HIWORD(wParam);
    RecreateFonts();
    LayoutFrame();
    InvalidateHeader();
    InvalidateBody();
}

bool CMainWindow::OnQueryEndSession(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    ctl_.RevertOnExit();
    lResult = TRUE;
    return true;
}

bool CMainWindow::OnEndSession(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    if (wp) ctl_.RevertOnExit();
    lResult = 0;
    return true;
}

void CMainWindow::PreCreate(LPCREATESTRUCT pcs) {
    pcs->dwExStyle |= WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
    pcs->style |= WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
}

bool CMainWindow::CreateMain() {
    int w = 960, h = 900, x = 0, y = 0;
    CenterOnWorkArea(w, h, x, y);
    wstring title = L"DesktopSplitter";
    if (!Controller::VersionText().empty())
        title += L" " + Controller::VersionText();
    return Create(WS_EX_APPWINDOW, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                  x, y, w, h, nullptr, nullptr);
}

BOOL CMainWindow::PreTranslateMessage(MSG* pMsg) {
    if (m_hWnd && ::IsDialogMessageW(m_hWnd, pMsg)) return TRUE;
    return FALSE;
}

bool CMainWindow::OnCreate(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    dpi_ = WindowDpi(m_hWnd);
    DarkTitle(m_hWnd);
    HICON icon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
    if (icon) {
        ::SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        ::SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
    brWindow_ = Solid(theme::WindowBackground);
    brPanel_ = Solid(theme::PanelBackground);
    brEdit_ = Solid(theme::ControlBackground);
    RecreateFonts();
    body_.host = this;
    body_.Create(WS_EX_CONTROLPARENT, L"",
                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                 0, 0, 100, 100, m_hWnd, (HMENU)(INT_PTR)IDC_BODY_SCROLL);
    ::ShowScrollBar(m_hWnd, SB_VERT, FALSE);
    RecreateChildren();
    ClearBodyNonClientScroll();

    ctl_.UiThreadHwnd = m_hWnd;
    ctl_.OnChanged = [this]() { SyncControls(); };
    ctl_.OnStatusAppended = [this]() {
        if (status_) {
            ::SetWindowTextW(status_, ctl_.StatusText().c_str());
            ::SendMessageW(status_, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            ::SendMessageW(status_, EM_SCROLLCARET, 0, 0);
        }
    };
    ctl_.OnMinimizeToTray = [this]() { HideToTray(); };

    confirmer_ = std::make_unique<UiApplyConfirmer>(m_hWnd, [this](const wstring& s){ ctl_.AppendStatus(s); });
    specUi_ = std::make_unique<UiSpecializationUi>(m_hWnd, [this]() {
        try { return DisplayConfig::Enumerate(); } catch (...) { return std::vector<MonitorInfo>{}; }
    });
    ctl_.confirmer_ = confirmer_.get();
    ctl_.specializationUi_ = specUi_.get();
    ctl_.LoadPreferences();
    ctl_.RecoverThenRefresh();
    ctl_.RefreshHiding();

    auto settings = SettingsStore::Load();
    minimizeToTray_ = settings ? settings->MinimizeToTrayOnClose : true;

    AddTray();
    trayCreatedMsg_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    SyncControls();
    LayoutFrame();
    ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    lResult = 0;
    return true;
}

bool CMainWindow::OnDestroy(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    RemoveTray();
    PostQuitMessage(0);
    lResult = 0;
    return true;
}

bool CMainWindow::OnClose(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    if (!exiting_ && trayAdded_ && minimizeToTray_) {
        HideToTray();
        lResult = 0;
        return true;
    }
    ExitApp();
    lResult = 0;
    return true;
}

void CMainWindow::ExitApp() {
    if (exiting_) return;
    exiting_ = true;
    ctl_.RevertOnExit();
    RemoveTray();
    DestroyWindow();
}

void CMainWindow::ShowFromTray() {
    ::ShowWindow(m_hWnd, SW_SHOW);
    if (::IsIconic(m_hWnd)) ::ShowWindow(m_hWnd, SW_RESTORE);
    ::SetForegroundWindow(m_hWnd);
}

void CMainWindow::HideToTray() {
    ::ShowWindow(m_hWnd, SW_HIDE);
    if (trayAdded_) {
        nid_.uFlags = NIF_INFO | NIF_MESSAGE | NIF_ICON | NIF_TIP;
        wcsncpy_s(nid_.szInfoTitle, L"DesktopSplitter", _TRUNCATE);
        wcsncpy_s(nid_.szInfo, L"Still running in the tray. Right-click the icon for Apply last / Revert / Exit.", _TRUNCATE);
        nid_.dwInfoFlags = NIIF_INFO;
        nid_.uTimeout = 3000;
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
}

void CMainWindow::RecreateFonts() {
    auto rec = [&](HFONT& f, double sz, int w, const wchar_t* face = L"Segoe UI") {
        if (f) DeleteObject(f);
        f = theme::CreateUiFont(dpi_, sz, w, face);
    };
    rec(fontTitle_, 19, FW_SEMIBOLD);
    rec(fontBody_, 12.5, FW_NORMAL);
    rec(fontMuted_, 11, FW_NORMAL);
    rec(fontHeader_, 11, FW_SEMIBOLD);
    rec(fontSmall_, 10.5, FW_NORMAL);
    rec(fontMono_, 13, FW_NORMAL, L"Consolas");
    rec(fontZone_, 30, FW_BOLD);
}

void CMainWindow::RecreateChildren() {
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    HWND body = BodyHwnd() ? BodyHwnd() : m_hWnd;
    auto btn = [&](HWND& w, const wchar_t* text, int id, HWND parent, bool primary=false) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
        w = ::CreateWindowExW(0, L"BUTTON", text, style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, inst, nullptr);
        ::SendMessageW(w, WM_SETFONT, (WPARAM)fontBody_, TRUE);
        (void)primary;
    };
    comboMonitor_ = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        0, 0, 10, 200, body, (HMENU)IDC_MONITOR, inst, nullptr);
    ::SendMessageW(comboMonitor_, WM_SETFONT, (WPARAM)fontBody_, TRUE);
    btn(btnRescan_, L"Rescan", IDC_RESCAN, body);
    chkStart_ = ::CreateWindowExW(0, L"BUTTON", L"Start DesktopSplitter with Windows",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,0,10,10, body, (HMENU)IDC_START_WITH_WINDOWS, inst, nullptr);
    chkAuto_ = ::CreateWindowExW(0, L"BUTTON", L"Re-apply the last split automatically when DesktopSplitter starts",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,0,10,10, body, (HMENU)IDC_APPLY_ON_STARTUP, inst, nullptr);
    ::SendMessageW(chkStart_, WM_SETFONT, (WPARAM)fontBody_, TRUE);
    ::SendMessageW(chkAuto_, WM_SETFONT, (WPARAM)fontBody_, TRUE);
    btn(btnReset_, L"Reset to layout defaults", IDC_RESET_DEFAULTS, body);
    btn(btnHideEn_, L"Enable hiding...", IDC_HIDE_ENABLE, body);
    btn(btnHideDis_, L"Disable hiding...", IDC_HIDE_DISABLE, body);
    btn(btnHideRecheck_, L"Re-check", IDC_HIDE_REFRESH, body);
    btn(btnApply_, L"Apply", IDC_APPLY, m_hWnd, true);
    btn(btnRevert_, L"Revert", IDC_REVERT, m_hWnd);
    btn(btnTray_, L"Minimize to tray", IDC_MINIMIZE_TRAY, m_hWnd);
    status_ = ::CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0,0,10,10, m_hWnd, (HMENU)IDC_STATUS, inst, nullptr);
    ::SendMessageW(status_, WM_SETFONT, (WPARAM)fontMuted_, TRUE);
    for (int i = 0; i < 4; ++i) {
        segWidth_[i] = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0,0,66,24, body, (HMENU)(INT_PTR)(IDC_SEG_WIDTH_BASE+i), inst, nullptr);
        segHeight_[i] = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0,0,66,24, body, (HMENU)(INT_PTR)(IDC_SEG_HEIGHT_BASE+i), inst, nullptr);
        ::SendMessageW(segWidth_[i], WM_SETFONT, (WPARAM)fontMono_, TRUE);
        ::SendMessageW(segHeight_[i], WM_SETFONT, (WPARAM)fontMono_, TRUE);
    }
    scrollBar_ = ::CreateWindowExW(0, L"SCROLLBAR", L"",
        WS_CHILD | WS_VISIBLE | SBS_VERT,
        0, 0, 16, 100, body, (HMENU)(INT_PTR)IDC_BODY_VSCROLL, inst, nullptr);
}

void CMainWindow::InvalidateHeader() {
    if (m_hWnd) ::InvalidateRect(m_hWnd, nullptr, FALSE);
}
void CMainWindow::InvalidateBody() {
    HWND body = BodyHwnd();
    if (!body) return;
    ::RedrawWindow(body, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void CMainWindow::RedrawBodyNow() {
    HWND body = BodyHwnd();
    if (!body) return;
    ::RedrawWindow(body, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

RECT CMainWindow::Vis(const RECT& doc) const {
    RECT r = doc;
    ::OffsetRect(&r, 0, -scrollY_);
    return r;
}

int CMainWindow::MeasureHeaderHeight(int innerW) const {
    int h = Dip(32); // title row
    HDC hdc = ::GetDC(m_hWnd ? m_hWnd : nullptr);
    auto textH = [&](HFONT font, const wstring& s, int minH) {
        if (!hdc || s.empty()) return minH;
        HFONT old = (HFONT)SelectObject(hdc, font ? font : fontMuted_);
        RECT r{ 0, 0, (std::max)(1, innerW), 0 };
        DrawTextW(hdc, s.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
        SelectObject(hdc, old);
        return (std::max)(minH, (int)(r.bottom - r.top));
    };
    h += Dip(4);
    h += textH(fontMuted_, ctl_.DriverStatus(), Dip(18));
    h += textH(fontMuted_, ctl_.CompositorStatus(), Dip(18));
    h += Dip(12); // gap before the body, matching WPF header Margin 0,0,0,12
    if (hdc) ::ReleaseDC(m_hWnd, hdc);
    return h;
}

DocLayout CMainWindow::MeasureDocument(int width) const {
    DocLayout d;
    d.width = width;
    const int x = 0;
    const int innerW = (std::max)(1, width);
    HDC hdc = ::GetDC(BodyHwnd() ? BodyHwnd() : m_hWnd);
    auto textH = [&](HFONT font, const wchar_t* s, int w, int minH) {
        if (!hdc || !s || !*s) return minH;
        HFONT old = (HFONT)SelectObject(hdc, font ? font : fontMuted_);
        RECT r{ 0, 0, (std::max)(1, w), 0 };
        DrawTextW(hdc, s, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
        SelectObject(hdc, old);
        return (std::max)(minH, (int)(r.bottom - r.top));
    };

    int y = 0;
    auto section = [&](RECT& dest) {
        dest = { x, y, x + innerW, y + Dip(18) };
        y += Dip(22);
    };

    section(d.rcSectionMonitor);
    int comboH = Dip(30);
    int rescanW = Dip(90);
    d.rcCombo = { x, y, x + innerW - rescanW - Dip(8), y + comboH };
    d.rcRescan = { x + innerW - rescanW, y, x + innerW, y + comboH };
    y += comboH + Dip(8);

    int cardH = Dip(52);
    int card1w = Dip(220);
    if (card1w > innerW) card1w = innerW;
    d.rcMode1 = { x, y, x + card1w, y + cardH };
    d.rcMode2 = { x + card1w + Dip(10), y, x + innerW, y + cardH };
    if (d.rcMode2.left > d.rcMode2.right) d.rcMode2.left = d.rcMode2.right;
    y += cardH + Dip(16);

    section(d.rcSectionStartup);
    d.rcStartupBox.left = x;
    d.rcStartupBox.top = y;
    d.rcStartupBox.right = x + innerW;
    int pad = Dip(12);
    int chkH = Dip(22);
    int iy = y + Dip(10);
    d.rcChkStart = { x + pad, iy, x + innerW - pad, iy + chkH };
    iy += chkH + Dip(8);
    d.rcChkAuto = { x + pad, iy, x + innerW - pad, iy + chkH };
    iy += chkH + Dip(8);
    const wchar_t* expl =
        L"The split is always torn down when DesktopSplitter exits, so the desktop is never left split with nothing driving it. A split that was still running at exit comes back on the next launch; one you reverted yourself does not.";
    int explH = textH(fontMuted_, expl, innerW - pad * 2, Dip(36));
    d.rcStartupExpl = { x + pad, iy, x + innerW - pad, iy + explH };
    iy = d.rcStartupExpl.bottom + Dip(8);
    d.rcStartupBox.bottom = iy;
    y = d.rcStartupBox.bottom + Dip(16);

    section(d.rcSectionLayout);
    int cardW = Dip(148) + Dip(8);
    int layH = Dip(84) + Dip(44);
    int maxCol = (std::max)(1, innerW / (std::max)(1, cardW));
    const auto& layouts = ctl_.Layouts();
    d.layoutCardCount = (int)layouts.size();
    d.rcLayouts = { x, y, x, y };
    for (int i = 0; i < d.layoutCardCount && i < 8; ++i) {
        int cx = x + (i % maxCol) * cardW;
        int cy = y + (i / maxCol) * (layH + Dip(4));
        RECT card = { cx, cy, cx + Dip(148), cy + layH };
        d.rcLayoutCards[i] = card;
        d.rcLayouts.right = (std::max)(d.rcLayouts.right, card.right);
        d.rcLayouts.bottom = (std::max)(d.rcLayouts.bottom, card.bottom);
    }
    if (d.layoutCardCount == 0) d.rcLayouts.bottom = y;
    y = d.rcLayouts.bottom + Dip(16);

    section(d.rcSectionZone);
    int framePad = Dip(14);
    int canvW = (int)ctl_.CanvasWidth();
    int canvH = (int)ctl_.CanvasHeight();
    d.rcEditorFrame = { x, y, x + (std::min)(innerW, canvW + framePad * 2), y + canvH + framePad * 2 };
    d.rcEditor = { d.rcEditorFrame.left + framePad, d.rcEditorFrame.top + framePad,
                   d.rcEditorFrame.left + framePad + canvW, d.rcEditorFrame.top + framePad + canvH };
    y = d.rcEditorFrame.bottom + Dip(8);

    d.hasSumNote = ctl_.HasSumNote();
    if (d.hasSumNote) {
        int noteH = textH(fontMuted_, ctl_.SumNote().c_str(), innerW, Dip(28));
        d.rcSumNote = { x, y, x + innerW, y + noteH };
        y += noteH + Dip(8);
    }

    int segW = Dip(212), segH = Dip(78), gap = Dip(8);
    const auto& segs = ctl_.Segments();
    d.segCount = (int)segs.size();
    int col = 0, row = 0;
    int segY0 = y;
    for (int i = 0; i < d.segCount && i < 4; ++i) {
        int sx = x + col * (segW + gap);
        int sy = segY0 + row * (segH + gap);
        d.rcSegCard[i] = { sx, sy, sx + segW, sy + segH };
        d.rcSegW[i] = { sx + Dip(28), sy + Dip(28), sx + Dip(28) + Dip(66), sy + Dip(28) + Dip(24) };
        d.rcSegH[i] = { sx + Dip(28) + Dip(66) + Dip(28), sy + Dip(28),
                        sx + Dip(28) + Dip(66) + Dip(28) + Dip(66), sy + Dip(28) + Dip(24) };
        ++col;
        if (col >= 3) { col = 0; ++row; }
    }
    int segRows = d.segCount == 0 ? 0 : (d.segCount + 2) / 3;
    y = segY0 + segRows * (segH + gap);
    if (segRows) y += Dip(0);

    d.rcReset = { x, y, x + Dip(200), y + Dip(30) };
    y = d.rcReset.bottom + Dip(18);

    section(d.rcSectionHiding);
    d.rcHidingPanel.left = x;
    d.rcHidingPanel.top = y;
    d.rcHidingPanel.right = x + innerW;
    int hy = y + Dip(12);
    const wchar_t* intro =
        L"Removes the physical monitor from the Windows desktop entirely instead of just covering it \u2014 the same thing as Settings > Advanced display > \"Remove display from desktop\". Enable only records the intent; the monitor is hidden during Apply, once segment 1 is primary, and it comes back automatically unless you confirm within 10 seconds. Revert always restores it.";
    int introH = textH(fontMuted_, intro, innerW - Dip(28), Dip(58));
    d.rcHidingIntro = { x + Dip(14), hy, x + innerW - Dip(14), hy + introH };
    hy = d.rcHidingIntro.bottom + Dip(6);
    hy += (int)ctl_.Hiding().Rows().size() * Dip(28);
    hy += Dip(8);
    d.rcHideEn = { x + Dip(14), hy, x + Dip(14) + Dip(140), hy + Dip(30) };
    d.rcHideDis = { x + Dip(14) + Dip(148), hy, x + Dip(14) + Dip(148) + Dip(140), hy + Dip(30) };
    d.rcHideRecheck = { x + Dip(14) + Dip(296), hy, x + Dip(14) + Dip(296) + Dip(100), hy + Dip(30) };
    hy += Dip(30) + Dip(8);
    d.hasDisabledReason = ctl_.HasDisabledReason();
    if (d.hasDisabledReason) {
        int rh = textH(fontMuted_, ctl_.DisabledReason().c_str(), innerW - Dip(28), Dip(20));
        d.rcDisabledReason = { x + Dip(14), hy, x + innerW - Dip(14), hy + rh };
        hy = d.rcDisabledReason.bottom + Dip(8);
    }
    d.rcHidingPanel.bottom = hy + Dip(4);
    y = d.rcHidingPanel.bottom + Dip(16);

    d.height = y;
    if (hdc) ::ReleaseDC(BodyHwnd() ? BodyHwnd() : m_hWnd, hdc);
    return d;
}

int CMainWindow::ScrollbarWidth() const {
    typedef int (WINAPI *Fn)(int, UINT);
    static Fn p = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
    if (p) return (std::max)(8, p(SM_CXVSCROLL, (UINT)(dpi_ ? dpi_ : 96)));
    return (std::max)(8, ::GetSystemMetrics(SM_CXVSCROLL));
}

void CMainWindow::LayoutFrame() {
    if (!m_hWnd || layoutInProgress_) return;
    layoutInProgress_ = true;

    RECT rc; ::GetClientRect(m_hWnd, &rc);
    int m = Dip(16);
    int innerW = (std::max)(1, (int)(rc.right - rc.left) - m * 2);
    int headerH = MeasureHeaderHeight(innerW);
    rcHeader_ = { m, m, rc.right - m, m + headerH };

    int statusH = Dip(170);
    int btnTopPad = Dip(14), btnH = Dip(32), btnBotPad = Dip(10);
    int btnBand = btnTopPad + btnH + btnBotPad;
    rcStatus_ = { m, rc.bottom - m - statusH, rc.right - m, rc.bottom - m };
    if (rcStatus_.top < rcHeader_.bottom) rcStatus_.top = rcHeader_.bottom;
    rcButtons_ = { m, rcStatus_.top - btnBand, rc.right - m, rcStatus_.top };
    if (rcButtons_.top < rcHeader_.bottom) rcButtons_.top = rcHeader_.bottom;

    rcBodyHost_ = { m, rcHeader_.bottom, rc.right - m, rcButtons_.top };
    if (rcBodyHost_.bottom < rcBodyHost_.top) rcBodyHost_.bottom = rcBodyHost_.top;

    if (BodyHwnd()) {
        ::MoveWindow(BodyHwnd(), rcBodyHost_.left, rcBodyHost_.top,
                     (std::max)(0, (int)(rcBodyHost_.right - rcBodyHost_.left)),
                     (std::max)(0, (int)(rcBodyHost_.bottom - rcBodyHost_.top)), TRUE);
    }

    int x = m;
    int by = rcButtons_.top + btnTopPad;
    if (btnApply_) ::MoveWindow(btnApply_, x, by, Dip(110), btnH, TRUE);
    if (btnRevert_) ::MoveWindow(btnRevert_, x + Dip(120), by, Dip(110), btnH, TRUE);
    if (btnTray_) ::MoveWindow(btnTray_, x + Dip(240), by, Dip(150), btnH, TRUE);
    if (status_) {
        ::MoveWindow(status_, rcStatus_.left, rcStatus_.top,
                     (std::max)(0, (int)(rcStatus_.right - rcStatus_.left)),
                     (std::max)(0, (int)(rcStatus_.bottom - rcStatus_.top)), TRUE);
    }

    LayoutDocument();
    layoutInProgress_ = false;
}

void CMainWindow::LayoutDocument() {
    HWND body = BodyHwnd();
    if (!body) return;

    ClearBodyNonClientScroll();
    RECT brc{}; ::GetClientRect(body, &brc);
    int sbW = ScrollbarWidth();
    int clientW = (std::max)(0, (int)(brc.right - brc.left));
    int clientH = (std::max)(0, (int)(brc.bottom - brc.top));
    if (scrollBar_ && sbW > 0) {
        ::SetWindowPos(scrollBar_, nullptr, (std::max)(0, clientW - sbW), 0, sbW, clientH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        RECT sbr{}; ::GetWindowRect(scrollBar_, &sbr);
        int actualW = (std::max)(0, (int)(sbr.right - sbr.left));
        if (actualW > 0 && actualW != sbW) {
            sbW = actualW;
            ::SetWindowPos(scrollBar_, nullptr, (std::max)(0, clientW - sbW), 0, sbW, clientH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        }
        ::ShowWindow(scrollBar_, SW_SHOW);
    }
    int width = (std::max)(0, scrollBar_ ? clientW - sbW : clientW);
    doc_ = MeasureDocument(width);
    contentH_ = doc_.height;
    UpdateScroll();

    PlaceBodyChildren();
}

void CMainWindow::PlaceBodyChildren() {
    HWND body = BodyHwnd();
    if (!body) return;

    struct Item { HWND w; RECT doc; bool show; };
    Item items[16]{};
    int n = 0;
    auto add = [&](HWND w, const RECT& doc, bool show) {
        if (w && n < 16) items[n++] = { w, doc, show };
    };
    add(comboMonitor_, doc_.rcCombo, true);
    add(btnRescan_, doc_.rcRescan, true);
    add(chkStart_, doc_.rcChkStart, true);
    add(chkAuto_, doc_.rcChkAuto, true);
    for (int i = 0; i < 4; ++i) {
        bool show = i < doc_.segCount;
        add(segWidth_[i], doc_.rcSegW[i], show);
        add(segHeight_[i], doc_.rcSegH[i], show);
    }
    add(btnReset_, doc_.rcReset, true);
    add(btnHideEn_, doc_.rcHideEn, true);
    add(btnHideDis_, doc_.rcHideDis, true);
    add(btnHideRecheck_, doc_.rcHideRecheck, true);

    HDWP hdwp = n > 0 ? ::BeginDeferWindowPos(n) : nullptr;
    for (int i = 0; i < n; ++i) {
        RECT r = Vis(items[i].doc);
        int ww = (std::max)(0, (int)(r.right - r.left));
        int hh = (std::max)(0, (int)(r.bottom - r.top));
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
        BOOL vis = ::IsWindowVisible(items[i].w);
        if (items[i].show && !vis) flags |= SWP_SHOWWINDOW;
        else if (!items[i].show && vis) flags |= SWP_HIDEWINDOW;
        if (hdwp)
            hdwp = ::DeferWindowPos(hdwp, items[i].w, nullptr, r.left, r.top, ww, hh, flags);
        else
            ::SetWindowPos(items[i].w, nullptr, r.left, r.top, ww, hh, flags);
    }
    if (hdwp) ::EndDeferWindowPos(hdwp);

    rcEditor_ = Vis(doc_.rcEditor);
    rcLayouts_ = Vis(doc_.rcLayouts);
    layoutCardCount_ = doc_.layoutCardCount;
    for (int i = 0; i < layoutCardCount_ && i < 8; ++i)
        rcLayoutCards_[i] = Vis(doc_.rcLayoutCards[i]);
}

void CMainWindow::ClearBodyNonClientScroll() {
    HWND body = BodyHwnd();
    if (!body) return;
    LONG style = ::GetWindowLongW(body, GWL_STYLE);
    if (style & WS_VSCROLL) {
        ::SetWindowLongW(body, GWL_STYLE, style & ~WS_VSCROLL);
        ::SetWindowPos(body, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    ::ShowScrollBar(body, SB_VERT, FALSE);
}

void CMainWindow::UpdateScroll() {
    HWND body = BodyHwnd();
    HWND sb = scrollBar_;
    if (!body || !sb) return;
    ClearBodyNonClientScroll();
    RECT rc; ::GetClientRect(body, &rc);
    int view = (std::max)(0, (int)(rc.bottom - rc.top));
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = (std::max)(0, contentH_ - 1);
    si.nPage = (UINT)view;
    si.nPos = scrollY_;
    ::SetScrollInfo(sb, SB_CTL, &si, TRUE);
    int maxPos = (std::max)(0, contentH_ - view);
    if (scrollY_ > maxPos) scrollY_ = maxPos;
    if (scrollY_ < 0) scrollY_ = 0;
}

bool CMainWindow::OnSize(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    dpi_ = WindowDpi(m_hWnd);
    LayoutFrame();
    InvalidateHeader();
    InvalidateBody();
    lResult = 0;
    return true;
}

bool CMainWindow::OnErase(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    lResult = 1;
    return true;
}

bool CMainWindow::OnVScroll(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    HWND body = BodyHwnd();
    if (!body) { lResult = 0; return true; }
    RECT rc; ::GetClientRect(body, &rc);
    int view = (std::max)(0, (int)(rc.bottom - rc.top));
    int maxPos = (std::max)(0, contentH_ - view);
    int code = LOWORD(wp);
    int pos = scrollY_;
    switch (code) {
    case SB_LINEUP: pos -= Dip(24); break;
    case SB_LINEDOWN: pos += Dip(24); break;
    case SB_PAGEUP: pos -= view * 3 / 4; break;
    case SB_PAGEDOWN: pos += view * 3 / 4; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        HWND sb = scrollBar_ ? scrollBar_ : body;
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        ::GetScrollInfo(sb, scrollBar_ ? SB_CTL : SB_VERT, &si);
        pos = si.nTrackPos;
        break;
    }
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = maxPos; break;
    default: lResult = 0; return true;
    }
    pos = Clampi(pos, 0, maxPos);
    if (pos != scrollY_) {
        scrollY_ = pos;
        PlaceBodyChildren();
        UpdateScroll();
        RedrawBodyNow();
    }
    lResult = 0;
    return true;
}

bool CMainWindow::OnMouseWheel(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    int delta = GET_WHEEL_DELTA_WPARAM(wp);
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) lines = 3;
    int step = Dip(24) * (int)lines;
    if (lines == WHEEL_PAGESCROLL) {
        LRESULT ignored = 0;
        OnVScroll(WM_VSCROLL, MAKEWPARAM(delta > 0 ? SB_PAGEUP : SB_PAGEDOWN, 0), 0, ignored);
    } else {
        HWND body = BodyHwnd();
        RECT rc{}; if (body) ::GetClientRect(body, &rc);
        int view = (std::max)(0, (int)(rc.bottom - rc.top));
        int maxPos = (std::max)(0, contentH_ - view);
        int pos = scrollY_ - (delta * step) / WHEEL_DELTA;
        pos = Clampi(pos, 0, maxPos);
        if (pos != scrollY_) {
            scrollY_ = pos;
            PlaceBodyChildren();
            UpdateScroll();
            RedrawBodyNow();
        }
    }
    lResult = 0;
    return true;
}

bool CMainWindow::OnDisplayChange(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    displayChangePending_ = true;
    ::SetTimer(m_hWnd, 10, 600, nullptr);
    lResult = 0;
    return true;
}

bool CMainWindow::OnTimer(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    if (wp == 10) {
        ::KillTimer(m_hWnd, 10);
        if (displayChangePending_) {
            displayChangePending_ = false;
            ctl_.OnDisplayChanged();
        }
    }
    lResult = 0;
    return true;
}

bool CMainWindow::OnWorkDone(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    ctl_.LoadMonitors(false);
    ctl_.RefreshDriverStatus();
    ctl_.RefreshCompositorStatus();
    ctl_.RefreshHiding();
    SyncControls();
    lResult = 0;
    return true;
}

bool CMainWindow::OnConfirmHop(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    struct Hop { const wstring* heading; const wstring* detail; int seconds; HANDLE ev; bool* result; };
    auto* hop = (Hop*)wp;
    if (!hop || !hop->result) { lResult = 1; return true; }
    *hop->result = CountdownDialog::Run(m_hWnd, hop->seconds, hop->heading ? *hop->heading : L"",
                                        hop->detail ? *hop->detail : L"", nullptr, true, hop->ev);
    lResult = 1;
    return true;
}

void CMainWindow::SyncControls() {
    // monitors combo
    if (comboMonitor_) {
        ::SendMessageW(comboMonitor_, CB_RESETCONTENT, 0, 0);
        for (const auto& m : ctl_.Monitors())
            ::SendMessageW(comboMonitor_, CB_ADDSTRING, 0, (LPARAM)m.Caption().c_str());
        if (ctl_.SelectedMonitorIndex() >= 0)
            ::SendMessageW(comboMonitor_, CB_SETCURSEL, ctl_.SelectedMonitorIndex(), 0);
    }
    ::SendMessageW(chkStart_, BM_SETCHECK, ctl_.StartWithWindows() ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(chkAuto_, BM_SETCHECK, ctl_.ApplyOnStartup() ? BST_CHECKED : BST_UNCHECKED, 0);
    ::EnableWindow(comboMonitor_, ctl_.IsIdle());
    ::EnableWindow(btnRescan_, ctl_.IsIdle());
    ::EnableWindow(chkStart_, ctl_.IsIdle());
    ::EnableWindow(chkAuto_, ctl_.IsIdle());
    ::EnableWindow(btnReset_, ctl_.IsIdle() && ctl_.SelectedLayout());
    ::EnableWindow(btnApply_, ctl_.CanApply());
    ::EnableWindow(btnRevert_, ctl_.IsIdle());
    bool hideBusy = ctl_.Hiding().SplitActive || !ctl_.IsIdle();
    ::EnableWindow(btnHideEn_, !hideBusy);
    ::EnableWindow(btnHideDis_, !hideBusy);
    ::EnableWindow(btnHideRecheck_, ctl_.IsIdle());
    const auto& segs = ctl_.Segments();
    for (int i = 0; i < 4; ++i) {
        if (i < (int)segs.size()) {
            // Don't stomp the box the user is typing in.
            HWND focus = ::GetFocus();
            if (focus != segWidth_[i]) ::SetWindowTextW(segWidth_[i], segs[i].WidthText.c_str());
            if (focus != segHeight_[i]) ::SetWindowTextW(segHeight_[i], segs[i].HeightText.c_str());
            ::EnableWindow(segWidth_[i], ctl_.IsIdle());
            ::EnableWindow(segHeight_[i], ctl_.IsIdle());
        }
    }
    if (status_) ::SetWindowTextW(status_, ctl_.StatusText().c_str());
    LayoutFrame();
    InvalidateHeader();
    InvalidateBody();
    wstring title = L"DesktopSplitter";
    if (!Controller::VersionText().empty()) title += L" " + Controller::VersionText();
    ::SetWindowTextW(m_hWnd, title.c_str());
}

bool CMainWindow::OnCommand(UINT, WPARAM wp, LPARAM, LRESULT& lResult) {
    int id = LOWORD(wp);
    int code = HIWORD(wp);
    if (id == IDC_MONITOR && code == CBN_SELCHANGE) {
        ctl_.SetSelectedMonitorIndex((int)::SendMessageW(comboMonitor_, CB_GETCURSEL, 0, 0));
        lResult = 0; return true;
    }
    if (code == EN_CHANGE && id >= IDC_SEG_WIDTH_BASE && id < IDC_SEG_WIDTH_BASE + 4) {
        int i = id - IDC_SEG_WIDTH_BASE;
        if (i < (int)ctl_.Segments().size()) {
            wchar_t buf[64]; ::GetWindowTextW(segWidth_[i], buf, 64);
            if (ctl_.Segments()[i].WidthText != buf) {
                ctl_.Segments()[i].SetTexts(buf, ctl_.Segments()[i].HeightText);
                if (!ctl_.IsDragging()) ctl_.RebuildEditor();
                SyncControls();
            }
        }
        lResult = 0; return true;
    }
    if (code == EN_CHANGE && id >= IDC_SEG_HEIGHT_BASE && id < IDC_SEG_HEIGHT_BASE + 4) {
        int i = id - IDC_SEG_HEIGHT_BASE;
        if (i < (int)ctl_.Segments().size()) {
            wchar_t buf[64]; ::GetWindowTextW(segHeight_[i], buf, 64);
            if (ctl_.Segments()[i].HeightText != buf) {
                ctl_.Segments()[i].SetTexts(ctl_.Segments()[i].WidthText, buf);
                if (!ctl_.IsDragging()) ctl_.RebuildEditor();
                SyncControls();
            }
        }
        lResult = 0; return true;
    }
    switch (id) {
    case IDC_RESCAN: ctl_.LoadMonitors(true); break;
    case IDC_START_WITH_WINDOWS: {
        bool on = ::SendMessageW(chkStart_, BM_GETCHECK, 0, 0) != BST_CHECKED;
        ::SendMessageW(chkStart_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        if (chkStart_) ::InvalidateRect(chkStart_, nullptr, FALSE);
        ctl_.SetStartWithWindows(on);
        break;
    }
    case IDC_APPLY_ON_STARTUP: {
        bool on = ::SendMessageW(chkAuto_, BM_GETCHECK, 0, 0) != BST_CHECKED;
        ::SendMessageW(chkAuto_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        if (chkAuto_) ::InvalidateRect(chkAuto_, nullptr, FALSE);
        ctl_.SetApplyOnStartup(on);
        break;
    }
    case IDC_RESET_DEFAULTS: ctl_.ResetSegments(); SyncControls(); break;
    case IDC_APPLY: ctl_.ApplyAsync(true); break;
    case IDC_REVERT: ctl_.RevertAsync(); break;
    case IDC_MINIMIZE_TRAY: HideToTray(); break;
    case IDC_HIDE_ENABLE: ctl_.EnableHiding(); break;
    case IDC_HIDE_DISABLE: ctl_.DisableHiding(); break;
    case IDC_HIDE_REFRESH: ctl_.RefreshHiding(); break;
    case IDM_TRAY_OPEN: ShowFromTray(); break;
    case IDM_TRAY_APPLY_LAST: ShowFromTray(); ctl_.ApplyLastAsync(); break;
    case IDM_TRAY_REVERT: ctl_.RevertAsync(); break;
    case IDM_TRAY_EXIT: ExitApp(); break;
    default: return false;
    }
    lResult = 0;
    return true;
}

bool CMainWindow::OnCtlColor(UINT msg, WPARAM wp, LPARAM lp, LRESULT& lResult) {
    HDC hdc = (HDC)wp;
    HWND ctl = (HWND)lp;
    if (ctl == status_ || (msg == WM_CTLCOLOREDIT)) {
        SetTextColor(hdc, theme::Foreground);
        SetBkColor(hdc, theme::PanelBackground);
        lResult = (LRESULT)brPanel_;
        return true;
    }
    SetTextColor(hdc, theme::Foreground);
    SetBkColor(hdc, theme::WindowBackground);
    SetBkMode(hdc, TRANSPARENT);
    lResult = (LRESULT)brWindow_;
    return true;
}

bool CMainWindow::OnMeasureItem(UINT, WPARAM, LPARAM lp, LRESULT& lResult) {
    auto* mis = (MEASUREITEMSTRUCT*)lp;
    if (mis->CtlType == ODT_COMBOBOX) {
        mis->itemHeight = Dip(26);
        lResult = 0;
        return true;
    }
    return false;
}

bool CMainWindow::OnDrawItem(UINT, WPARAM, LPARAM lp, LRESULT& lResult) {
    auto* dis = (DRAWITEMSTRUCT*)lp;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
    if (dis->CtlType == ODT_COMBOBOX) {
        COLORREF bg = theme::ControlBackground;
        if (dis->itemState & ODS_COMBOBOXEDIT) bg = theme::ControlBackground;
        else if (dis->itemState & ODS_SELECTED) bg = theme::AccentSoft;
        else if (dis->itemState & ODS_HOTLIGHT) bg = theme::ComboItemHover;
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(hdc, &rc, br); DeleteObject(br);
        if ((int)dis->itemID >= 0) {
            wchar_t buf[512];
            ::SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)buf);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme::Foreground);
            HFONT old = (HFONT)SelectObject(hdc, fontBody_);
            RECT t = rc; t.left += Dip(9);
            DrawTextW(hdc, buf, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
        }
        lResult = TRUE;
        return true;
    }
    if (dis->CtlType == ODT_BUTTON &&
        (dis->hwndItem == chkStart_ || dis->hwndItem == chkAuto_)) {
        DrawCheckbox(dis);
        lResult = TRUE;
        return true;
    }
    if (dis->CtlType == ODT_BUTTON) {
        bool primary = (dis->hwndItem == btnApply_);
        COLORREF bg = primary ? theme::AccentSoft : theme::PanelBackground;
        COLORREF bd = primary ? theme::Accent : theme::PanelBorder;
        if (hover || selected) bg = theme::ButtonHover;
        if (primary && (hover || selected)) bg = RGB(0x32, 0x58, 0x96);
        DrawRoundRect(hdc, rc, Dip(4), bg, bd, 1);
        wchar_t text[128]; ::GetWindowTextW(dis->hwndItem, text, 128);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, disabled ? theme::Muted : theme::Foreground);
        HFONT old = (HFONT)SelectObject(hdc, primary ? fontHeader_ : fontBody_);
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        lResult = TRUE;
        return true;
    }
    return false;
}

void CMainWindow::DrawCheckbox(const DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool checked = ::SendMessageW(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
    HBRUSH br = CreateSolidBrush(theme::PanelBackground);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    int box = Dip(16);
    int y = rc.top + ((rc.bottom - rc.top) - box) / 2;
    if (y < rc.top) y = rc.top;
    RECT bx = { rc.left, y, rc.left + box, y + box };
    COLORREF fill = checked ? theme::AccentSoft : theme::ControlBackground;
    COLORREF bd = checked ? theme::Accent : theme::PanelBorder;
    if (dis->itemState & ODS_FOCUS) bd = theme::Accent;
    DrawRoundRect(hdc, bx, Dip(2), fill, bd, 1);
    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, (std::max)(1, Dip(2)), theme::Foreground);
        HGDIOBJ oldP = SelectObject(hdc, pen);
        int x0 = bx.left + Dip(3);
        int y0 = bx.top + box / 2;
        int x1 = bx.left + box / 2 - Dip(1);
        int y1 = bx.bottom - Dip(4);
        int x2 = bx.right - Dip(3);
        int y2 = bx.top + Dip(4);
        MoveToEx(hdc, x0, y0, nullptr);
        LineTo(hdc, x1, y1);
        LineTo(hdc, x2, y2);
        SelectObject(hdc, oldP);
        DeleteObject(pen);
    }

    wchar_t text[256];
    ::GetWindowTextW(dis->hwndItem, text, 256);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? theme::Muted : theme::Foreground);
    HFONT oldF = (HFONT)SelectObject(hdc, fontBody_);
    RECT tr = rc;
    tr.left = bx.right + Dip(8);
    DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldF);
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = tr;
        DrawFocusRect(hdc, &fr);
    }
}

void CMainWindow::DrawRoundRect(HDC hdc, RECT rc, int radius, COLORREF fill, COLORREF border, int borderW) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderW, border);
    HGDIOBJ oldB = SelectObject(hdc, br);
    HGDIOBJ oldP = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(hdc, oldB);
    SelectObject(hdc, oldP);
    DeleteObject(br);
    DeleteObject(pen);
}


bool CMainWindow::OnPaint(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    PAINTSTRUCT ps; HDC hdc = ::BeginPaint(m_hWnd, &ps);
    RECT rc; ::GetClientRect(m_hWnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, (std::max)(1, (int)rc.right), (std::max)(1, (int)rc.bottom));
    HGDIOBJ old = SelectObject(mem, bmp);
    PaintHeader(mem, rc);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ::EndPaint(m_hWnd, &ps);
    lResult = 0;
    return true;
}

void CMainWindow::PaintHeader(HDC hdc, const RECT& rc) {
    HBRUSH br = CreateSolidBrush(theme::WindowBackground);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    if (BodyHwnd()) {
        RECT body = rcBodyHost_;
        ::ExcludeClipRect(hdc, body.left, body.top, body.right, body.bottom);
    }
    SetBkMode(hdc, TRANSPARENT);
    int m = Dip(16);
    int x = m;
    int y = m;
    int innerW = (std::max)(1, (int)(rc.right - rc.left) - m * 2);

    HFONT old = (HFONT)SelectObject(hdc, fontTitle_);
    SetTextColor(hdc, theme::Foreground);
    RECT r = { x, y, x + innerW, y + Dip(28) };
    DrawTextW(hdc, L"DesktopSplitter", -1, &r, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    SIZE titleSz{};
    GetTextExtentPoint32W(hdc, L"DesktopSplitter", 15, &titleSz);
    SelectObject(hdc, fontMuted_);
    SetTextColor(hdc, theme::Muted);
    RECT rv = { x + titleSz.cx + Dip(8), y + Dip(8), x + innerW, y + Dip(28) };
    DrawTextW(hdc, Controller::VersionText().c_str(), -1, &rv, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    y += Dip(32) + Dip(4);

    SelectObject(hdc, fontMuted_);
    SetTextColor(hdc, theme::Muted);
    RECT rd = { x, y, x + innerW, rcHeader_.bottom };
    DrawTextW(hdc, ctl_.DriverStatus().c_str(), -1, &rd, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    rd.right = x + innerW;
    DrawTextW(hdc, ctl_.DriverStatus().c_str(), -1, &rd, DT_LEFT | DT_WORDBREAK);
    y = rd.bottom;
    RECT rcst = { x, y, x + innerW, rcHeader_.bottom };
    DrawTextW(hdc, ctl_.CompositorStatus().c_str(), -1, &rcst, DT_LEFT | DT_WORDBREAK);
    SelectObject(hdc, old);
}

void CMainWindow::PaintBody(HDC hdc, const RECT& rc) {
    HBRUSH br = CreateSolidBrush(theme::WindowBackground);
    FillRect(hdc, &rc, br); DeleteObject(br);
    SetBkMode(hdc, TRANSPARENT);
    if (scrollBar_) {
        int contentRight = (std::max)((int)rc.left, (int)rc.right - ScrollbarWidth());
        IntersectClipRect(hdc, rc.left, rc.top, contentRight, rc.bottom);
    }

    auto section = [&](const RECT& docR, const wchar_t* title) {
        RECT r = Vis(docR);
        SelectObject(hdc, fontHeader_);
        SetTextColor(hdc, theme::Muted);
        DrawTextW(hdc, title, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };

    section(doc_.rcSectionMonitor, L"PHYSICAL MONITOR");

    auto card = [&](const RECT& docR, const wchar_t* label, const wstring& value) {
        RECT c = Vis(docR);
        DrawRoundRect(hdc, c, Dip(4), theme::PanelBackground, theme::PanelBorder, 1);
        SelectObject(hdc, fontHeader_);
        SetTextColor(hdc, theme::Muted);
        RECT t = { c.left + Dip(10), c.top + Dip(6), c.right - Dip(8), c.top + Dip(20) };
        DrawTextW(hdc, label, -1, &t, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);
        SelectObject(hdc, fontMono_);
        SetTextColor(hdc, theme::Foreground);
        RECT v = { c.left + Dip(10), c.top + Dip(22), c.right - Dip(8), c.bottom - Dip(4) };
        DrawTextW(hdc, value.c_str(), -1, &v, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);
    };
    card(doc_.rcMode1, L"CURRENT MODE", ctl_.CurrentModeText());
    card(doc_.rcMode2, L"MAX REFRESH \u2014 ALL ZONES RUN AT THIS RATE", ctl_.RefreshText());

    section(doc_.rcSectionStartup, L"STARTUP");
    RECT startBox = Vis(doc_.rcStartupBox);
    DrawRoundRect(hdc, startBox, Dip(4), theme::PanelBackground, theme::PanelBorder, 1);
    SelectObject(hdc, fontMuted_);
    SetTextColor(hdc, theme::Muted);
    RECT expl = Vis(doc_.rcStartupExpl);
    DrawTextW(hdc,
        L"The split is always torn down when DesktopSplitter exits, so the desktop is never left split with nothing driving it. A split that was still running at exit comes back on the next launch; one you reverted yourself does not.",
        -1, &expl, DT_LEFT | DT_WORDBREAK);

    section(doc_.rcSectionLayout, L"LAYOUT");
    rcLayouts_ = Vis(doc_.rcLayouts);
    layoutCardCount_ = doc_.layoutCardCount;
    for (int i = 0; i < layoutCardCount_ && i < 8; ++i)
        rcLayoutCards_[i] = Vis(doc_.rcLayoutCards[i]);
    PaintLayoutPicker(hdc);

    section(doc_.rcSectionZone, L"ZONE EDITOR \u2014 DRAG THE BORDERS, OR TYPE EXACT SIZES BELOW");
    rcEditor_ = Vis(doc_.rcEditor);
    PaintZoneEditor(hdc);
    if (doc_.hasSumNote) {
        SelectObject(hdc, fontMuted_);
        SetTextColor(hdc, theme::Muted);
        RECT sn = Vis(doc_.rcSumNote);
        DrawTextW(hdc, ctl_.SumNote().c_str(), -1, &sn, DT_LEFT | DT_WORDBREAK);
    }

    const auto& segs = ctl_.Segments();
    for (int i = 0; i < doc_.segCount && i < (int)segs.size(); ++i) {
        RECT cardRc = Vis(doc_.rcSegCard[i]);
        DrawRoundRect(hdc, cardRc, Dip(4), theme::PanelBackground, theme::PanelBorder, 1);
        SelectObject(hdc, fontHeader_);
        SetTextColor(hdc, theme::Foreground);
        RECT tt = { cardRc.left + Dip(10), cardRc.top + Dip(7), cardRc.right - Dip(10), cardRc.top + Dip(24) };
        DrawTextW(hdc, segs[i].Title().c_str(), -1, &tt, DT_LEFT);
        SelectObject(hdc, fontMuted_);
        SetTextColor(hdc, theme::Muted);
        RECT lw = { cardRc.left + Dip(10), cardRc.top + Dip(32), cardRc.left + Dip(26), cardRc.top + Dip(52) };
        DrawTextW(hdc, L"W", -1, &lw, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT lh = { cardRc.left + Dip(10) + Dip(66) + Dip(28), cardRc.top + Dip(32),
                    cardRc.left + Dip(26) + Dip(66) + Dip(28), cardRc.top + Dip(52) };
        DrawTextW(hdc, L"H", -1, &lh, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (segs[i].HasError()) {
            SelectObject(hdc, fontSmall_);
            SetTextColor(hdc, theme::Error);
            RECT er = { cardRc.left + Dip(10), cardRc.top + Dip(54), cardRc.right - Dip(8), cardRc.bottom - Dip(4) };
            DrawTextW(hdc, segs[i].Error.c_str(), -1, &er, DT_LEFT | DT_WORDBREAK);
        }
    }

    section(doc_.rcSectionHiding, L"DISPLAY HIDING (EXPERIMENTAL)");
    PaintHiding(hdc);
}

void CMainWindow::PaintLayoutPicker(HDC hdc) {
    const auto& layouts = ctl_.Layouts();
    for (int i = 0; i < layoutCardCount_ && i < (int)layouts.size(); ++i) {
        RECT card = rcLayoutCards_[i];
        bool sel = (i == ctl_.SelectedLayoutIndex());
        COLORREF bg = sel ? theme::LayoutSelected : theme::WindowBackground;
        COLORREF bd = sel ? theme::Accent : RGB(0, 0, 0);
        if (sel) DrawRoundRect(hdc, card, Dip(5), bg, bd, 2);
        RECT prev = { card.left + Dip(4), card.top + Dip(6), card.left + Dip(4) + Dip(140), card.top + Dip(6) + Dip(84) };
        DrawRoundRect(hdc, prev, Dip(3), theme::PreviewBackground, theme::PanelBorder, 1);
        const auto& boxes = layouts[i].PreviewBoxes;
        for (const auto& b : boxes) {
            RECT zr = {
                prev.left + Dip(4) + (int)std::lround(b.X),
                prev.top + Dip(4) + (int)std::lround(b.Y),
                0, 0
            };
            zr.right = zr.left + (int)std::lround(b.W);
            zr.bottom = zr.top + (int)std::lround(b.H);
            HBRUSH fill = CreateSolidBrush(theme::SegmentFill);
            HPEN pen = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(hdc, fill);
            HGDIOBJ oP = SelectObject(hdc, pen);
            RoundRect(hdc, zr.left, zr.top, zr.right, zr.bottom, Dip(2) * 2, Dip(2) * 2);
            SelectObject(hdc, oB); SelectObject(hdc, oP);
            DeleteObject(fill); DeleteObject(pen);
        }
        SelectObject(hdc, fontBody_);
        SetTextColor(hdc, theme::Foreground);
        RECT nm = { card.left, prev.bottom + Dip(6), card.right, prev.bottom + Dip(24) };
        DrawTextW(hdc, layouts[i].Name.c_str(), -1, &nm, DT_CENTER | DT_WORDBREAK);
        SelectObject(hdc, fontSmall_);
        SetTextColor(hdc, theme::Muted);
        RECT ds = { card.left, nm.bottom, card.right, card.bottom };
        DrawTextW(hdc, layouts[i].Description.c_str(), -1, &ds, DT_CENTER | DT_WORDBREAK);
    }
}

void CMainWindow::PaintZoneEditor(HDC hdc) {
    RECT frame = Vis(doc_.rcEditorFrame);
    DrawRoundRect(hdc, frame, Dip(5), theme::CanvasBackground, theme::PanelBorder, 1);
    const auto& zones = ctl_.Zones();
    for (const auto& z : zones) {
        RECT zr = {
            rcEditor_.left + (int)std::lround(z.X),
            rcEditor_.top + (int)std::lround(z.Y),
            rcEditor_.left + (int)std::lround(z.X + z.W),
            rcEditor_.top + (int)std::lround(z.Y + z.H)
        };
        DrawRoundRect(hdc, zr, Dip(3), theme::ZoneFill, theme::ZoneStroke, 1);
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, fontZone_);
        SetTextColor(hdc, theme::ZoneNumber);
        RECT num = zr; num.bottom = (zr.top + zr.bottom) / 2 + Dip(8);
        wchar_t n[8]; swprintf_s(n, L"%d", z.Number());
        DrawTextW(hdc, n, -1, &num, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        SelectObject(hdc, fontBody_);
        SetTextColor(hdc, theme::Foreground);
        RECT sz = zr; sz.top = num.bottom + Dip(1);
        DrawTextW(hdc, z.SizeText.c_str(), -1, &sz, DT_CENTER | DT_TOP | DT_SINGLELINE);
        if (z.HasAspectNote()) {
            SIZE ext{};
            GetTextExtentPoint32W(hdc, z.AspectNote.c_str(), (int)z.AspectNote.size(), &ext);
            int bw = ext.cx + Dip(14), bh = ext.cy + Dip(4);
            RECT badge = {
                (zr.left + zr.right - bw) / 2,
                sz.top + Dip(16),
                0, 0
            };
            badge.right = badge.left + bw;
            badge.bottom = badge.top + bh;
            DrawRoundRect(hdc, badge, Dip(8), theme::AccentSoft, theme::ZoneStroke, 1);
            SelectObject(hdc, fontSmall_);
            DrawTextW(hdc, z.AspectNote.c_str(), -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    const auto& splitters = ctl_.Splitters();
    for (const auto& s : splitters) {
        RECT sr = {
            rcEditor_.left + (int)std::lround(s.X),
            rcEditor_.top + (int)std::lround(s.Y),
            rcEditor_.left + (int)std::lround(s.X + s.W()),
            rcEditor_.top + (int)std::lround(s.Y + s.H())
        };
        bool hover = (hoverSplitter_ == s.Id) || (dragging_ && ctl_.IsDragging());
        COLORREF c = hover ? theme::SplitterHover : theme::Splitter;
        DrawRoundRect(hdc, sr, Dip(3), c, c, 1);
    }
}

void CMainWindow::PaintHiding(HDC hdc) {
    RECT panel = Vis(doc_.rcHidingPanel);
    DrawRoundRect(hdc, panel, Dip(4), theme::PanelBackground, theme::PanelBorder, 1);
    SelectObject(hdc, fontMuted_);
    SetTextColor(hdc, theme::Muted);
    RECT intro = Vis(doc_.rcHidingIntro);
    DrawTextW(hdc,
        L"Removes the physical monitor from the Windows desktop entirely instead of just covering it \u2014 the same thing as Settings > Advanced display > \"Remove display from desktop\". Enable only records the intent; the monitor is hidden during Apply, once segment 1 is primary, and it comes back automatically unless you confirm within 10 seconds. Revert always restores it.",
        -1, &intro, DT_LEFT | DT_WORDBREAK);
    int y = intro.bottom + Dip(6);
    for (const auto& row : ctl_.Hiding().Rows()) {
        COLORREF gc = theme::Muted;
        if (row.State == CheckState::Ok) gc = theme::OkGreen;
        else if (row.State == CheckState::No) gc = theme::WarnAmber;
        SelectObject(hdc, fontBody_);
        SetTextColor(hdc, gc);
        RECT g = { panel.left + Dip(14), y, panel.left + Dip(32), y + Dip(20) };
        DrawTextW(hdc, row.Glyph().c_str(), -1, &g, DT_LEFT);
        SetTextColor(hdc, theme::Foreground);
        RECT lab = { panel.left + Dip(14) + Dip(18), y, panel.left + Dip(32) + Dip(176), y + Dip(20) };
        DrawTextW(hdc, row.Label.c_str(), -1, &lab, DT_LEFT);
        SelectObject(hdc, fontMuted_);
        SetTextColor(hdc, theme::Muted);
        RECT det = { lab.right + Dip(8), y, panel.right - Dip(14), y + Dip(36) };
        DrawTextW(hdc, row.Detail.c_str(), -1, &det, DT_LEFT | DT_WORDBREAK);
        y += Dip(28);
    }
    if (doc_.hasDisabledReason) {
        SetTextColor(hdc, theme::WarnAmber);
        RECT dr = Vis(doc_.rcDisabledReason);
        DrawTextW(hdc, ctl_.DisabledReason().c_str(), -1, &dr, DT_LEFT | DT_WORDBREAK);
    }
}

int CMainWindow::HitLayout(int x, int y) const {
    for (int i = 0; i < layoutCardCount_; ++i) {
        if (::PtInRect(&rcLayoutCards_[i], POINT{ x, y })) return i;
    }
    return -1;
}
int CMainWindow::HitSplitter(int x, int y) const {
    for (const auto& s : ctl_.Splitters()) {
        RECT sr = {
            rcEditor_.left + (int)std::lround(s.X),
            rcEditor_.top + (int)std::lround(s.Y),
            rcEditor_.left + (int)std::lround(s.X + s.W()),
            rcEditor_.top + (int)std::lround(s.Y + s.H())
        };
        if (::PtInRect(&sr, POINT{ x, y })) return s.Id;
    }
    return -1;
}

bool CMainWindow::OnLButtonDown(UINT, WPARAM, LPARAM lp, LRESULT& lResult) {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    int lay = HitLayout(x, y);
    if (lay >= 0 && ctl_.IsIdle()) {
        ctl_.SetSelectedLayoutIndex(lay);
        lResult = 0; return true;
    }
    int sp = HitSplitter(x, y);
    if (sp >= 0 && ctl_.IsIdle()) {
        dragging_ = true;
        ::SetCapture(BodyHwnd());
        ctl_.BeginSplitterDrag(sp, x - rcEditor_.left, y - rcEditor_.top);
        InvalidateBody();
        lResult = 0; return true;
    }
    return false;
}
bool CMainWindow::OnMouseMove(UINT, WPARAM wp, LPARAM lp, LRESULT& lResult) {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (dragging_ && (wp & MK_LBUTTON)) {
        ctl_.UpdateSplitterDrag(x - rcEditor_.left, y - rcEditor_.top);
        SyncControls();
        lResult = 0; return true;
    }
    int sp = HitSplitter(x, y);
    if (sp != hoverSplitter_) { hoverSplitter_ = sp; InvalidateBody(); }
    return false;
}
bool CMainWindow::OnLButtonUp(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    if (dragging_) {
        dragging_ = false;
        ::ReleaseCapture();
        ctl_.EndSplitterDrag();
        SyncControls();
        lResult = 0; return true;
    }
    return false;
}
bool CMainWindow::OnSetCursor(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    POINT pt; ::GetCursorPos(&pt); ::ScreenToClient(m_hWnd, &pt);
    int sp = HitSplitter(pt.x, pt.y);
    if (sp >= 0) {
        for (const auto& s : ctl_.Splitters()) if (s.Id == sp) {
            ::SetCursor(::LoadCursor(nullptr, s.IsVertical ? IDC_SIZEWE : IDC_SIZENS));
            lResult = TRUE; return true;
        }
    }
    return false;
}

void CMainWindow::AddTray() {
    memset(&nid_, 0, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = m_hWnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kTrayCallback;
    nid_.hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
    if (!nid_.hIcon) nid_.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid_.szTip, L"DesktopSplitter", _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != 0;
    if (trayAdded_) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    }
}
void CMainWindow::RemoveTray() {
    if (!trayAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    trayAdded_ = false;
}
void CMainWindow::ShowTrayMenu() {
    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Open DesktopSplitter");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_TRAY_APPLY_LAST, L"Apply last configuration");
    ::AppendMenuW(menu, MF_STRING, IDM_TRAY_REVERT, L"Revert");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
    POINT pt; ::GetCursorPos(&pt);
    ::SetForegroundWindow(m_hWnd);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
}
bool CMainWindow::OnTrayCallback(UINT, WPARAM, LPARAM lp, LRESULT& lResult) {
    UINT ev = LOWORD(lp);
    if (ev == WM_LBUTTONDBLCLK || ev == NIN_SELECT || ev == NIN_KEYSELECT) ShowFromTray();
    else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) ShowTrayMenu();
    lResult = 0;
    return true;
}


void CBodyWindow::SetWindowClassEx(WNDCLASSEX& wce) {
    TWL::CUserWindow::SetWindowClassEx(wce);
    wce.style |= CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wce.hbrBackground = nullptr;
    wce.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
}
void CBodyWindow::PreCreate(LPCREATESTRUCT pcs) {
    pcs->dwExStyle |= WS_EX_CONTROLPARENT;
    pcs->style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
}
bool CBodyWindow::OnErase(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    lResult = 1;
    return true;
}
bool CBodyWindow::OnPaint(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    if (!host) return false;
    PAINTSTRUCT ps; HDC hdc = ::BeginPaint(m_hWnd, &ps);
    RECT rc; ::GetClientRect(m_hWnd, &rc);
    int w = (std::max)(1, (int)rc.right), h = (std::max)(1, (int)rc.bottom);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    host->PaintBody(mem, rc);
    // Never bit-blit the backbuffer over child HWNDs (edits, checkboxes, etc.).
    for (HWND ch = ::GetWindow(m_hWnd, GW_CHILD); ch; ch = ::GetWindow(ch, GW_HWNDNEXT)) {
        if (!::IsWindowVisible(ch)) continue;
        RECT cr{};
        ::GetWindowRect(ch, &cr);
        ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&cr), 2);
        ::ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
    }
    int px = ps.rcPaint.left, py = ps.rcPaint.top;
    int pw = (std::max)(0, (int)(ps.rcPaint.right - ps.rcPaint.left));
    int ph = (std::max)(0, (int)(ps.rcPaint.bottom - ps.rcPaint.top));
    if (pw > 0 && ph > 0)
        ::BitBlt(hdc, px, py, pw, ph, mem, px, py, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ::EndPaint(m_hWnd, &ps);
    lResult = 0;
    return true;
}
bool CBodyWindow::OnSize(UINT, WPARAM, LPARAM, LRESULT& lResult) {
    if (host && !host->layoutInProgress_) host->LayoutDocument();
    lResult = 0;
    return true;
}
bool CBodyWindow::OnVScroll(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnVScroll(m, w, l, r) : false;
}
bool CBodyWindow::OnMouseWheel(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnMouseWheel(m, w, l, r) : false;
}
bool CBodyWindow::OnLButtonDown(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnLButtonDown(m, w, l, r) : false;
}
bool CBodyWindow::OnLButtonUp(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnLButtonUp(m, w, l, r) : false;
}
bool CBodyWindow::OnMouseMove(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnMouseMove(m, w, l, r) : false;
}
bool CBodyWindow::OnSetCursor(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnSetCursor(m, w, l, r) : false;
}
bool CBodyWindow::OnCommand(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnCommand(m, w, l, r) : false;
}
bool CBodyWindow::OnCtlColor(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnCtlColor(m, w, l, r) : false;
}
bool CBodyWindow::OnDrawItem(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnDrawItem(m, w, l, r) : false;
}
bool CBodyWindow::OnMeasureItem(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
    return host ? host->OnMeasureItem(m, w, l, r) : false;
}

} // namespace ds
