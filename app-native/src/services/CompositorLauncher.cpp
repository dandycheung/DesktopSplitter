#include "services/CompositorLauncher.h"
#include <tlhelp32.h>

namespace ds {
namespace {

wstring JoinPath(const wstring& a, const wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

wstring ParentDir(const wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == wstring::npos ? L"" : path.substr(0, s);
}

bool FileExists(const wstring& p) {
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const wstring& p) {
    DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

HANDLE OpenProc(DWORD pid) {
    return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
}

} // namespace

wstring CompositorLauncher::BaseDirectory() {
    wstring exe = /* Current module dir */ {};
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    if (!n) return L".";
    return ParentDir(buf);
}

wstring CompositorLauncher::ExpectedPath() {
    wstring dir = BaseDirectory();
    for (int up = 0; up <= 5 && !dir.empty(); ++up) {
        wstring candidate = JoinPath(dir, L"compositor");
        if (DirExists(candidate))
            return JoinPath(JoinPath(JoinPath(JoinPath(candidate, L"build"), L"x64"), L"Release"), ExecutableName);
        dir = ParentDir(dir);
    }
    return JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(JoinPath(BaseDirectory(), L".."), L".."), L".."), L".."),
                                 L"compositor"), L"build\\x64\\Release"), ExecutableName);
}

bool CompositorLauncher::TryFindExecutable(wstring& path) {
    path.clear();
    std::vector<wstring> probes;
    probes.push_back(ExecutableName);
    const wchar_t* configs[] = { L"Release", L"Debug" };
    for (int up = 0; up <= 5; ++up) {
        wstring prefix;
        for (int i = 0; i < up; ++i) prefix += L"..\\";
        for (auto* c : configs)
            probes.push_back(prefix + L"compositor\\build\\x64\\" + c + L"\\" + ExecutableName);
    }
    wstring base = BaseDirectory();
    for (const auto& probe : probes) {
        wchar_t full[MAX_PATH * 4];
        wstring cand = JoinPath(base, probe);
        if (GetFullPathNameW(cand.c_str(), ARRAYSIZE(full), full, nullptr)) cand = full;
        if (FileExists(cand)) { path = cand; return true; }
    }
    return false;
}

std::vector<DWORD> CompositorLauncher::GetRunningPids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, ExecutableName) == 0)
                pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

bool CompositorLauncher::IsRunning() { return !GetRunningPids().empty(); }

SpecializedWait CompositorLauncher::WaitForSpecializedLive(
    DWORD pid, int timeoutMs, HANDLE cancelEvent, const std::function<void(const wstring&)>& log)
{
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutMs;
    HANDLE live = nullptr;
    SpecializedWait result = SpecializedWait::TimedOut;
    HANDLE proc = OpenProc(pid);

    while (GetTickCount64() < deadline) {
        if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
            result = SpecializedWait::TimedOut;
            break;
        }
        if (!live) {
            live = OpenEventW(SYNCHRONIZE, FALSE, SpecializedLiveEventName);
            if (live && log) log(L"Opened " + wstring(SpecializedLiveEventName) + L".");
        }
        if (live && WaitForSingleObject(live, 0) == WAIT_OBJECT_0) {
            result = SpecializedWait::Live;
            break;
        }
        if (proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(proc, &code);
            if (log) log(L"Compositor exited with code " + std::to_wstring(code) +
                         L" before confirming specialized presentation.");
            result = SpecializedWait::CompositorExited;
            break;
        }
        Sleep(100);
    }
    if (live) CloseHandle(live);
    if (proc) CloseHandle(proc);
    return result;
}

DWORD CompositorLauncher::Start(const std::vector<wstring>& arguments) {
    wstring exe;
    if (!TryFindExecutable(exe)) {
        throw CompositorNotFoundException(
            Narrow(L"Compositor not found. Build it and place dscomp.exe at:\r\n    " + ExpectedPath() +
                   L"\r\n(or next to DesktopSplitter.exe in " + BaseDirectory() + L")."));
    }
    wstring cmd = L"\"" + exe + L"\"";
    for (const auto& a : arguments) {
        cmd += L" ";
        if (a.find(L' ') != wstring::npos) cmd += L"\"" + a + L"\"";
        else cmd += a;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    wstring work = ParentDir(exe);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);
    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, work.c_str(), &si, &pi)) {
        throw CompositorNotFoundException(Narrow(L"Failed to start '" + exe + L"'."));
    }
    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pid;
}

bool CompositorLauncher::SignalStop() {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, StopEventName);
    if (!h) return false;
    BOOL ok = SetEvent(h);
    CloseHandle(h);
    return ok != 0;
}

bool CompositorLauncher::StopAndWait(int timeoutMs, const std::function<void(const wstring&)>& log) {
    auto running = GetRunningPids();
    if (running.empty()) {
        if (log) log(L"Compositor is not running.");
        SignalStop();
        return true;
    }
    bool signalled = SignalStop();
    if (log) log(signalled
        ? (L"Signalled " + wstring(StopEventName) + L"; waiting for the compositor to exit.")
        : (L"Could not open " + wstring(StopEventName) + L"; falling back to WM_CLOSE."));
    if (!signalled) {
        // best-effort: no reliable main window; skip WM_CLOSE equivalent
    }
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutMs;
    for (DWORD pid : running) {
        HANDLE p = OpenProc(pid);
        if (!p) continue;
        DWORD remaining = (DWORD)(std::max)(0ULL, deadline - GetTickCount64());
        if (WaitForSingleObject(p, remaining) != WAIT_OBJECT_0) {
            if (log) log(L"Compositor (pid " + std::to_wstring(pid) +
                         L") did not exit within the timeout \u2014 terminating it.");
            TerminateProcess(p, 1);
            WaitForSingleObject(p, 3000);
        }
        CloseHandle(p);
    }
    return GetRunningPids().empty();
}

} // namespace ds
