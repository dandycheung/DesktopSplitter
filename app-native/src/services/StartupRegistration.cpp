#include "services/StartupRegistration.h"
#include <shlwapi.h>

namespace ds {
namespace {
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kValueName[] = L"DesktopSplitter";
}

wstring StartupRegistration::CurrentExecutablePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    if (n == 0) return L"DesktopSplitter.exe";
    wchar_t full[MAX_PATH * 4];
    if (GetFullPathNameW(buf, ARRAYSIZE(full), full, nullptr)) return full;
    return buf;
}

wstring StartupRegistration::ExpectedCommand() {
    return L"\"" + CurrentExecutablePath() + L"\" " + StartupArgument;
}

bool StartupRegistration::LaunchedAtStartup(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], StartupArgument) == 0) return true;
    return false;
}

StartupState StartupRegistration::Read() {
    StartupState st;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return st;
    wchar_t value[4096];
    DWORD type = 0, size = sizeof(value);
    LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, (LPBYTE)value, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return st;
    // size includes null when REG_SZ
    wstring command = value;
    if (Trim(command).empty()) return st;
    st.Enabled = true;
    st.RegisteredCommand = command;
    wstring registered = ExtractExecutable(command);
    if (registered.empty()) { st.PointsAtCurrent = false; return st; }
    wchar_t a[MAX_PATH * 4], b[MAX_PATH * 4];
    if (GetFullPathNameW(registered.c_str(), ARRAYSIZE(a), a, nullptr) &&
        GetFullPathNameW(CurrentExecutablePath().c_str(), ARRAYSIZE(b), b, nullptr))
        st.PointsAtCurrent = _wcsicmp(a, b) == 0;
    else
        st.PointsAtCurrent = EqualsIgnoreCase(registered, CurrentExecutablePath());
    return st;
}

bool StartupRegistration::Enable(const std::function<void(const wstring&)>& log) {
    try {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Run key could not be opened.");
        wstring cmd = ExpectedCommand();
        LONG rc = RegSetValueExW(key, kValueName, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                                 (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS) throw std::runtime_error("RegSetValueEx failed");
        if (log) log(L"DesktopSplitter will start with Windows (" + cmd + L").");
        return true;
    } catch (const std::exception& ex) {
        if (log) log(L"Could not enable start with Windows: " + Widen(ex.what()));
        return false;
    }
}

bool StartupRegistration::Disable(const std::function<void(const wstring&)>& log) {
    try {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, kValueName);
            RegCloseKey(key);
            if (log) log(L"DesktopSplitter will no longer start with Windows.");
        }
        return true;
    } catch (const std::exception& ex) {
        if (log) log(L"Could not disable start with Windows: " + Widen(ex.what()));
        return false;
    }
}

bool StartupRegistration::Set(bool enabled, const std::function<void(const wstring&)>& log) {
    return enabled ? Enable(log) : Disable(log);
}

void StartupRegistration::RefreshIfStale(const std::function<void(const wstring&)>& log) {
    StartupState state = Read();
    if (!state.NeedsRefresh()) return;
    if (log) log(L"Updating the start-with-Windows entry: it pointed at " + state.RegisteredCommand + L".");
    Enable(log);
}

wstring StartupRegistration::ExtractExecutable(const wstring& command) {
    wstring c = Trim(command);
    if (c.empty()) return {};
    if (c[0] == L'"') {
        size_t close = c.find(L'"', 1);
        return close > 1 ? c.substr(1, close - 1) : Trim(c.substr(1));
    }
    auto pos = c.find(L".exe");
    if (pos == wstring::npos) {
        pos = c.find(L".EXE");
    }
    // case-insensitive .exe
    size_t exe = wstring::npos;
    for (size_t i = 0; i + 4 <= c.size(); ++i) {
        if (_wcsnicmp(c.c_str() + i, L".exe", 4) == 0) { exe = i; break; }
    }
    if (exe != wstring::npos) return c.substr(0, exe + 4);
    size_t sp = c.find(L' ');
    return sp == wstring::npos ? c : c.substr(0, sp);
}

} // namespace ds
