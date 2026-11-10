#include "services/HidingStatus.h"
#include "services/SettingsStore.h"
#include "services/CompositorLauncher.h"
#include "interop/DisplayConfig.h"

namespace ds {
namespace {

const wchar_t kDeskSplitKey[] = L"SOFTWARE\\DesktopSplitter";
const wchar_t kHideFlagValue[] = L"HidePhysicalDisplay";

std::optional<int> ReadDword(HKEY root, const wchar_t* subKey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return std::nullopt;
    DWORD type = 0, value = 0, size = sizeof(value);
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)&value, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return std::nullopt;
    return (int)value;
}

HidingSnapshot Degenerate(const wstring& detail, const StatusRow& intent, bool intentOn) {
    HidingSnapshot s;
    s.SystemSupport = { L"Windows can hide displays", CheckState::Unknown, detail };
    s.MonitorSupport = { L"This monitor can be hidden", CheckState::Unknown, detail };
    s.CurrentlySpecialized = { L"Hidden right now", CheckState::Unknown, detail };
    s.Intent = intent;
    s.SplitActive = HidingStatus::IsSplitActive();
    s.Available = false;
    s.EnabledNow = false;
    s.IntentOn = intentOn;
    return s;
}

} // namespace

bool HidingStatus::IsSplitActive() {
    try {
        for (const auto& m : DisplayConfig::Enumerate())
            if (m.IsDeskSplitVirtual) return true;
    } catch (const DisplayConfigException&) {}
    return CompositorLauncher::IsRunning();
}

bool HidingStatus::ReadIntent() {
    auto machine = ReadDword(HKEY_LOCAL_MACHINE, kDeskSplitKey, kHideFlagValue);
    if (machine) return *machine != 0;
    auto settings = SettingsStore::Load();
    return settings && settings->HidePhysicalDisplay;
}

HidingSnapshot HidingStatus::Query(const MonitorInfo* selected) {
    bool intentOn = ReadIntent();
    StatusRow intent = intentOn
        ? StatusRow{ L"Hide during Apply", CheckState::Ok,
            L"On \u2014 Apply will remove the physical monitor from the desktop once the split is up." }
        : StatusRow{ L"Hide during Apply", CheckState::No,
            L"Off \u2014 the compositor just covers the physical monitor (the default)." };

    if (!selected) return Degenerate(L"No monitor selected.", intent, intentOn);

    SpecializationState state;
    int rc = 0;
    if (!MonitorSpecialization::TryGet(*selected, state, rc)) {
        wstring detail = L"DisplayConfig GET (type 12) failed with error " + std::to_wstring(rc);
        if (rc == MonitorSpecialization::ERROR_INVALID_PARAMETER_)
            detail += L" \u2014 this Windows build may not support monitor specialization.";
        else
            detail += L".";
        return Degenerate(detail, intent, intentOn);
    }

    HidingSnapshot s;
    s.SystemSupport = { L"Windows can hide displays",
        state.AvailableForSystem() ? CheckState::Ok : CheckState::No,
        state.AvailableForSystem()
            ? L"Yes \u2014 this Windows edition allows displays to be removed from the desktop."
            : L"No \u2014 this Windows edition does not allow displays to be removed from the desktop." };
    wchar_t raw[32];
    swprintf_s(raw, L"0x%X", state.RawValue);
    s.MonitorSupport = { L"This monitor can be hidden",
        state.AvailableForMonitor() ? CheckState::Ok : CheckState::No,
        state.AvailableForMonitor()
            ? (L"Yes \u2014 " + selected->DisplayLabel() + L" is eligible (capability only; GET raw " + raw + L").")
            : (L"No \u2014 " + selected->DisplayLabel() + L" is not eligible (GET raw " + raw + L").") };
    s.CurrentlySpecialized = { L"Hidden right now",
        state.EnabledNow() ? CheckState::Ok : CheckState::Info,
        state.EnabledNow()
            ? (L"YES \u2014 " + selected->DisplayLabel() + L" is off the desktop right now.")
            : (L"No \u2014 " + selected->DisplayLabel() + L" is a normal desktop display right now.") };
    s.Intent = intent;
    s.SplitActive = IsSplitActive();
    s.Available = state.Available();
    s.EnabledNow = state.EnabledNow();
    s.IntentOn = intentOn;
    return s;
}

HidingStatus::IntentWriteResult HidingStatus::WriteIntent(bool enabled) {
    AppSettings settings = SettingsStore::Load().value_or(AppSettings{});
    settings.HidePhysicalDisplay = enabled;
    try {
        SettingsStore::Save(settings);
    } catch (const std::exception& ex) {
        return { false, false, L"Could not save the setting: " + Widen(ex.what()) };
    }

    wstring onOff = enabled ? L"enabled" : L"disabled";
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kDeskSplitKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        return { true, false,
            L"Display hiding " + onOff + L" for this user. The machine-wide flag (HKLM\\" +
            wstring(kDeskSplitKey) + L"\\" + kHideFlagValue +
            L") needs administrator rights and was left alone \u2014 DesktopSplitter passes --specialized to the compositor "
            L"itself, so this only matters if you run dscomp.exe standalone." };
    }
    DWORD value = enabled ? 1 : 0;
    rc = RegSetValueExW(key, kHideFlagValue, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        return { true, false,
            L"Display hiding " + onOff + L" for this user; the machine-wide flag could not be written." };
    }
    return { true, true, L"Display hiding " + onOff + L" (saved for this user and machine-wide)." };
}

} // namespace ds
