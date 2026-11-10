#pragma once
#include "models/AppSettings.h"
#include "util/JsonIo.h"

namespace ds {

class SettingsStore {
public:
    static wstring Directory() { return GetAppDataDir(); }
    static wstring FilePath() { return SettingsFilePath(); }

    // Returns nullopt on missing or corrupt file — must never prevent startup.
    static std::optional<AppSettings> Load();
    static void Save(const AppSettings& settings);
};

string CompositorConfigToJson(const CompositorConfig& cfg);

} // namespace ds
