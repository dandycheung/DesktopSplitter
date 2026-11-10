#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "interop/DisplayConfig.h"
#include <shellapi.h>
#include <dwmapi.h>

namespace ds {
namespace {

HWND g_activeCountdown = nullptr;
bool g_countdownKeep = false;
bool g_countdownAnswered = false;

void EnableDarkTitle(HWND hwnd) {
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+); 19 is the older value.
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

HFONT UiFont(HWND hwnd, double size, int weight = FW_NORMAL) {
    int dpi = 96;
    HDC hdc = GetDC(hwnd);
    if (hdc) { dpi = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(hwnd, hdc); }
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn p = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (p) dpi = (int)p(hwnd);
    return theme::CreateUiFont(dpi, size, weight);
}

void PlaceRect(HWND hwnd, const PixelRect& r) {
    SetWindowPos(hwnd, HWND_TOPMOST, r.X, r.Y, r.W, r.H, SWP_SHOWWINDOW);
}

struct CountdownState {
    int remaining = 0;
    wstring heading, detail;
    HWND number = nullptr;
    HWND keep = nullptr;
    HWND revert = nullptr;
    HANDLE cancel = nullptr;
    HFONT fontTitle = nullptr, fontBody = nullptr, fontNum = nullptr;
};

LRESULT CALLBACK CountdownProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (CountdownState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        st = (CountdownState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        EnableDarkTitle(hwnd);
        st->fontTitle = UiFont(hwnd, 16, FW_SEMIBOLD);
        st->fontBody = UiFont(hwnd, 12);
        st->fontNum = UiFont(hwnd, 28, FW_SEMIBOLD);
        st->number = CreateWindowExW(0, L"STATIC", std::to_wstring(st->remaining).c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 90, 420, 48, hwnd, nullptr, cs->hInstance, nullptr);
        st->keep = CreateWindowExW(0, L"BUTTON", L"Keep", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            70, 200, 140, 32, hwnd, (HMENU)1, cs->hInstance, nullptr);
        st->revert = CreateWindowExW(0, L"BUTTON", L"Revert", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            230, 200, 140, 32, hwnd, (HMENU)2, cs->hInstance, nullptr);
        SendMessageW(st->number, WM_SETFONT, (WPARAM)st->fontNum, TRUE);
        SendMessageW(st->keep, WM_SETFONT, (WPARAM)st->fontTitle, TRUE);
        SendMessageW(st->revert, WM_SETFONT, (WPARAM)st->fontBody, TRUE);
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, theme::Foreground);
        SetBkColor(hdc, theme::WindowBackground);
        static HBRUSH br = CreateSolidBrush(theme::WindowBackground);
        return (LRESULT)br;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(theme::WindowBackground);
        FillRect((HDC)wp, &rc, br);
        DeleteObject(br);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(theme::WindowBackground);
        FillRect(hdc, &rc, br); DeleteObject(br);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, theme::Foreground);
        HFONT old = (HFONT)SelectObject(hdc, st->fontTitle);
        RECT r = { 24, 18, rc.right - 24, 70 };
        DrawTextW(hdc, st->heading.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK);
        SelectObject(hdc, st->fontBody);
        SetTextColor(hdc, theme::Muted);
        RECT r2 = { 24, 150, rc.right - 24, 200 };
        DrawTextW(hdc, st->detail.c_str(), -1, &r2, DT_LEFT | DT_WORDBREAK);
        SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (!st) break;
        if (st->cancel && WaitForSingleObject(st->cancel, 0) == WAIT_OBJECT_0) {
            g_countdownKeep = false; g_countdownAnswered = true; DestroyWindow(hwnd); return 0;
        }
        st->remaining--;
        if (st->remaining <= 0) {
            g_countdownKeep = false; g_countdownAnswered = true; DestroyWindow(hwnd); return 0;
        }
        SetWindowTextW(st->number, std::to_wstring(st->remaining).c_str());
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) { g_countdownKeep = true; g_countdownAnswered = true; DestroyWindow(hwnd); }
        if (LOWORD(wp) == 2) { g_countdownKeep = false; g_countdownAnswered = true; DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        g_countdownKeep = false; g_countdownAnswered = true; DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (st) {
            if (st->fontTitle) DeleteObject(st->fontTitle);
            if (st->fontBody) DeleteObject(st->fontBody);
            if (st->fontNum) DeleteObject(st->fontNum);
        }
        if (g_activeCountdown == hwnd) g_activeCountdown = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PumpUntilAnswered(HWND hwnd, HANDLE cancelEvent) {
    MSG msg;
    while (g_activeCountdown && !g_countdownAnswered) {
        if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
            g_countdownKeep = false; g_countdownAnswered = true;
            if (g_activeCountdown) DestroyWindow(g_activeCountdown);
            break;
        }
        DWORD wr = MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return false; }
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        (void)wr;
    }
    return g_countdownKeep;
}

} // namespace

bool CountdownDialog::Run(HWND owner, int seconds, const wstring& heading, const wstring& detail,
                          const MonitorInfo* placeOn, bool placeOnPrimary, HANDLE cancelEvent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CountdownProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopSplitterCountdown";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        registered = true;
    }
    CountdownState st;
    st.remaining = seconds;
    st.heading = heading;
    st.detail = detail;
    st.cancel = cancelEvent;
    g_countdownKeep = false;
    g_countdownAnswered = false;

    int w = 460, h = 260;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"DesktopSplitterCountdown",
        L"DesktopSplitter", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h, owner, nullptr, GetModuleHandleW(nullptr), &st);
    g_activeCountdown = hwnd;
    if (!hwnd) return true; // headless fallback like WPF

    PixelRect place{ 0,0,w,h };
    if (placeOn) {
        place = CountdownPlacement::Center(*placeOn, w, h);
        PlaceRect(hwnd, place);
    } else if (placeOnPrimary) {
        auto monitors = DisplayConfig::Enumerate();
        const MonitorInfo* primary = nullptr;
        for (const auto& m : monitors) if (m.IsPrimary) { primary = &m; break; }
        if (primary) place = CountdownPlacement::Center(*primary, w, h);
        PlaceRect(hwnd, place);
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    bool keep = PumpUntilAnswered(hwnd, cancelEvent);
    return keep;
}

void CountdownDialog::CloseActive() {
    if (g_activeCountdown) {
        g_countdownKeep = false;
        g_countdownAnswered = true;
        DestroyWindow(g_activeCountdown);
    }
}

void GuidedSettingsDialog::OpenDisplaySettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:display-advanced", nullptr, nullptr, SW_SHOWNORMAL);
}

struct GuidedState {
    MonitorInfo target;
    bool wantEnabled = false;
    wstring reason;
    HANDLE cancel = nullptr;
    bool finishedOk = false;
    bool done = false;
    HFONT fontTitle = nullptr, fontBody = nullptr;
};

LRESULT CALLBACK GuidedProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (GuidedState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        st = (GuidedState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        EnableDarkTitle(hwnd);
        st->fontTitle = UiFont(hwnd, 14, FW_SEMIBOLD);
        st->fontBody = UiFont(hwnd, 12);
        CreateWindowExW(0, L"BUTTON", L"Open Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            24, 280, 150, 32, hwnd, (HMENU)1, cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Done", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            190, 280, 110, 32, hwnd, (HMENU)2, cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            310, 280, 110, 32, hwnd, (HMENU)3, cs->hInstance, nullptr);
        EnumChildWindows(hwnd, [](HWND c, LPARAM f) {
            SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return TRUE;
        }, (LPARAM)st->fontBody);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(theme::WindowBackground);
        FillRect((HDC)wp, &rc, br); DeleteObject(br);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(theme::WindowBackground);
        FillRect(hdc, &rc, br); DeleteObject(br);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, theme::Foreground);
        HFONT old = (HFONT)SelectObject(hdc, st->fontTitle);
        wstring title = st->wantEnabled
            ? L"Remove display from desktop"
            : L"Restore display to the desktop";
        RECT r = { 24, 16, rc.right - 24, 50 };
        DrawTextW(hdc, title.c_str(), -1, &r, DT_LEFT);
        SelectObject(hdc, st->fontBody);
        SetTextColor(hdc, theme::Muted);
        wstring body = L"In Settings > System > Display > Advanced display, select " +
            st->target.DisplayLabel() + L" and turn " + (st->wantEnabled ? L"ON" : L"OFF") +
            L" \"Remove display from desktop\".\r\n\r\n" + st->reason;
        RECT r2 = { 24, 56, rc.right - 24, 270 };
        DrawTextW(hdc, body.c_str(), -1, &r2, DT_LEFT | DT_WORDBREAK);
        SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 1) GuidedSettingsDialog::OpenDisplaySettings();
        if (LOWORD(wp) == 2) { st->finishedOk = true; st->done = true; DestroyWindow(hwnd); }
        if (LOWORD(wp) == 3) { st->finishedOk = false; st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        st->finishedOk = false; st->done = true; DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (st) {
            if (st->fontTitle) DeleteObject(st->fontTitle);
            if (st->fontBody) DeleteObject(st->fontBody);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool GuidedSettingsDialog::Run(HWND owner, const MonitorInfo& target, bool wantEnabled,
                               const wstring& reason, const MonitorInfo* placeOn, HANDLE cancelEvent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = GuidedProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopSplitterGuided";
        RegisterClassExW(&wc);
        registered = true;
    }
    GuidedState st;
    st.target = target;
    st.wantEnabled = wantEnabled;
    st.reason = reason;
    st.cancel = cancelEvent;
    int w = 460, h = 360;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"DesktopSplitterGuided", L"DesktopSplitter",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (placeOn) {
        auto place = CountdownPlacement::Center(*placeOn, w, h);
        PlaceRect(hwnd, place);
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    OpenDisplaySettings();
    MSG msg;
    while (hwnd && IsWindow(hwnd) && !st.done) {
        if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
            st.finishedOk = false; st.done = true; DestroyWindow(hwnd); break;
        }
        // Poll until the requested specialization state is reached, like the WPF window.
        SpecializationState state; int rc = 0;
        if (MonitorSpecialization::TryGet(target, state, rc)) {
            if (wantEnabled && state.EnabledNow()) { st.finishedOk = true; st.done = true; DestroyWindow(hwnd); break; }
            if (!wantEnabled && !state.EnabledNow()) { st.finishedOk = true; st.done = true; DestroyWindow(hwnd); break; }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 400, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return false; }
            if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
    }
    return st.finishedOk;
}

UiApplyConfirmer::UiApplyConfirmer(HWND owner, std::function<void(const wstring&)> log)
    : owner_(owner), log_(std::move(log)) {}

bool UiApplyConfirmer::ConfirmApply(const wstring& summary, int timeoutSeconds, HANDLE cancelEvent) {
    if (!owner_) return true;
    wstring heading = L"Keep these display settings?";
    wstring detail = L"Applied: " + summary + L".\r\nIf anything looks wrong, or you cannot see this clearly, do nothing \u2014"
                     L"the previous settings come back when the countdown reaches zero.";
    // This is called from a background thread. Marshal with SendMessage? We run a nested
    // message loop on this thread after hopping to the UI thread via SendMessage callback.
    struct Pack { UiApplyConfirmer* self; const wstring* heading; const wstring* detail; int seconds; HANDLE ev; bool result; };
    Pack pack{ this, &heading, &detail, timeoutSeconds, cancelEvent, false };
    auto fn = [](HWND hwnd, UINT, ULONG_PTR data, LRESULT*) -> void {
        auto* p = (Pack*)data;
        p->result = CountdownDialog::Run(hwnd, p->seconds, *p->heading, *p->detail, nullptr, true, p->ev);
    };
    SENDASYNCPROC unused = nullptr; (void)unused;
    // SendMessageCallback is async; we need a blocking UI-thread call.
    // Use a custom message handled on the owner? Safer: SendMessage with a registered message.
    static UINT msg = RegisterWindowMessageW(L"DesktopSplitter.ConfirmApply");
    // Fall back to attaching and running on this thread if we can't post.
    // The owner window's thread should process this via SendMessage.
    // We'll just call CountdownDialog::Run from this thread — creating a window on a
    // worker thread is not ideal, so hop with SendMessage to the owner.
    struct Hop { const wstring* heading; const wstring* detail; int seconds; HANDLE ev; bool* result; };
    static thread_local Hop* tls = nullptr;
    Hop hop{ &heading, &detail, timeoutSeconds, cancelEvent, &pack.result };
    tls = &hop;
    // Use SendMessage to owner with a private message; MainWindow must handle it.
    // If unhandled, run here anyway so Apply still confirms.
    LRESULT handled = SendMessageW(owner_, WM_APP + 40, (WPARAM)&hop, 0);
    if (!handled) {
        pack.result = CountdownDialog::Run(owner_, timeoutSeconds, heading, detail, nullptr, true, cancelEvent);
    }
    if (!pack.result && log_)
        log_(L"No answer within " + std::to_wstring(timeoutSeconds) + L"s \u2014 reverting the change automatically.");
    return pack.result;
}

UiSpecializationUi::UiSpecializationUi(HWND owner, std::function<std::vector<MonitorInfo>()> monitors)
    : owner_(owner), monitors_(std::move(monitors)) {}

bool UiSpecializationUi::GuideManualEnable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE ev) {
    auto list = monitors_ ? monitors_() : std::vector<MonitorInfo>{};
    const MonitorInfo* place = CountdownPlacement::ChooseMonitor(list, physical.DeviceName);
    return GuidedSettingsDialog::Run(owner_, physical, true, failure.Message, place, ev);
}

bool UiSpecializationUi::ConfirmKeep(const MonitorInfo& physical, int timeoutSeconds, HANDLE ev) {
    auto list = monitors_ ? monitors_() : std::vector<MonitorInfo>{};
    const MonitorInfo* place = CountdownPlacement::ChooseMonitor(list, physical.DeviceName);
    return CountdownDialog::Run(owner_, timeoutSeconds,
        L"Keep these display settings?",
        L"The physical monitor was removed from the desktop. If anything looks wrong, or you cannot see this clearly, do nothing \u2014"
        L"the previous settings come back when the countdown reaches zero.",
        place, false, ev);
}

void UiSpecializationUi::GuideManualDisable(const MonitorInfo& physical, const SpecializationResult& failure, HANDLE ev) {
    auto list = monitors_ ? monitors_() : std::vector<MonitorInfo>{};
    const MonitorInfo* place = CountdownPlacement::ChooseMonitor(list, physical.DeviceName);
    GuidedSettingsDialog::Run(owner_, physical, false, failure.Message, place, ev);
}

} // namespace ds
