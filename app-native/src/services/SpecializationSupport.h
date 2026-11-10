#pragma once
#include "models/Types.h"

namespace ds {

struct AdminFlowResult {
    bool Success = false;
    bool NotAttempted = true;
    wstring Message;
};

class SystemSettingsAdminFlowsClient {
public:
    static AdminFlowResult TryRun(const MonitorInfo& physical, bool enable,
                                  const std::function<void(const wstring&)>& log,
                                  HANDLE cancelEvent);
};

class SpecializationHashStore {
public:
    static void SeedKnownHashes(const std::vector<MonitorInfo>& monitors,
                                const std::function<void(const wstring&)>& log);
    static void Learn(const MonitorInfo& physical, const wstring& hash,
                      const std::function<void(const wstring&)>& log);
    static std::optional<wstring> Lookup(const MonitorInfo& physical);
};

struct SpecializeDisplayInvocation {
    wstring Hash;
};

class SpecializationCaptureWatcher {
public:
    SpecializationCaptureWatcher(const MonitorInfo& physical,
                                 const std::function<void(const wstring&)>& log);
    void Start();
    std::optional<SpecializeDisplayInvocation> StopAndCollect();
    bool ElevationRequired() const { return elevationRequired_; }
private:
    MonitorInfo physical_;
    std::function<void(const wstring&)> log_;
    bool elevationRequired_ = false;
    bool started_ = false;
};

} // namespace ds
