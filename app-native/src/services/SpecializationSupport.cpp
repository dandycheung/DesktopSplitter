#include "services/SpecializationSupport.h"
#include "services/SettingsStore.h"

namespace ds {

AdminFlowResult SystemSettingsAdminFlowsClient::TryRun(const MonitorInfo& physical, bool /*enable*/,
                                                       const std::function<void(const wstring&)>& /*log*/,
                                                       HANDLE /*cancelEvent*/) {
    auto hash = SpecializationHashStore::Lookup(physical);
    if (!hash) {
        return { false, true, L"no validated identity hash for this monitor is stored yet." };
    }
    // The automatic SystemSettingsAdminFlows.exe route is implemented in a follow-up
    // once the captured command line is ported 1:1. A stored hash still unlocks that
    // path in the WPF app; until the native helper is finished we fall through to the
    // guided Settings dialog, which is the same user-visible outcome.
    return { false, true, L"automatic SpecializeDisplay route is not yet wired in the native app; using the Windows Settings route." };
}

void SpecializationHashStore::SeedKnownHashes(const std::vector<MonitorInfo>& /*monitors*/,
                                              const std::function<void(const wstring&)>& /*log*/) {
    // Hashes already live in settings.json; nothing to seed beyond a load, which Query/Learn do.
}

void SpecializationHashStore::Learn(const MonitorInfo& physical, const wstring& hash,
                                    const std::function<void(const wstring&)>& log) {
    if (physical.MonitorDevicePath.empty() || hash.empty()) return;
    AppSettings s = SettingsStore::Load().value_or(AppSettings{});
    s.SpecializationHashes[physical.MonitorDevicePath] = hash;
    try {
        SettingsStore::Save(s);
        if (log) log(L"Remembered the identity hash for " + physical.DisplayLabel() + L".");
    } catch (...) {}
}

std::optional<wstring> SpecializationHashStore::Lookup(const MonitorInfo& physical) {
    auto s = SettingsStore::Load();
    if (!s) return std::nullopt;
    for (const auto& kv : s->SpecializationHashes) {
        if (EqualsIgnoreCase(kv.first, physical.MonitorDevicePath)) return kv.second;
    }
    return std::nullopt;
}

SpecializationCaptureWatcher::SpecializationCaptureWatcher(const MonitorInfo& physical,
                                                           const std::function<void(const wstring&)>& log)
    : physical_(physical), log_(log) {}

void SpecializationCaptureWatcher::Start() { started_ = true; }

std::optional<SpecializeDisplayInvocation> SpecializationCaptureWatcher::StopAndCollect() {
    started_ = false;
    return std::nullopt;
}

} // namespace ds
