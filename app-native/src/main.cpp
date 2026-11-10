#include "Common.h"
#include "ui/MainWindow.h"
#include "services/StartupRegistration.h"
#include <commctrl.h>
#include <shellapi.h>

using namespace ds;

namespace {
CMainWindow* g_main = nullptr;
volatile long g_teardownDone = 0;

void Teardown() {
    if (InterlockedExchange(&g_teardownDone, 1) != 0) return;
    try {
        if (g_main) g_main->GetController().RevertOnExit();
    } catch (...) {
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) {
    Teardown();
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    SetUnhandledExceptionFilter(CrashFilter);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool launchedAtStartup = StartupRegistration::LaunchedAtStartup(argc, argv);
    if (argv) LocalFree(argv);

    CMainWindow wnd;
    g_main = &wnd;
    if (!wnd.CreateMain()) {
        g_main = nullptr;
        MessageBoxW(nullptr, L"Could not create the DesktopSplitter window.", L"DesktopSplitter",
                    MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }

    // Auto-started at logon: go straight to the tray rather than popping the window open.
    if (!launchedAtStartup) {
        int show = (nCmdShow == SW_HIDE || nCmdShow == 0) ? SW_SHOW : nCmdShow;
        ShowWindow(wnd, show);
        UpdateWindow(wnd);
    }

    wnd.GetController().AutoApplyOnStartup();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!wnd.PreTranslateMessage(&msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    Teardown();
    g_main = nullptr;
    if (SUCCEEDED(com)) CoUninitialize();
    return (int)msg.wParam;
}
