#include "services/SettingsStore.h"

namespace ds {

static AppSettings FromJson(const JsonValue& root) {
    AppSettings s;
    if (root.Type != JsonType::Object) return s;
    s.PhysicalDevice = root.GetString(L"physicalDevice");
    s.PhysicalFriendlyName = root.GetString(L"physicalFriendlyName");
    s.Layout = (LayoutKind)root.GetInt(L"layout", (int)LayoutKind::TwoVertical);
    if (auto* segs = root.Find(L"segments"); segs && segs->Type == JsonType::Array) {
        for (const auto& item : segs->Array) {
            SegmentSize sz;
            sz.Width = item.GetInt(L"width");
            sz.Height = item.GetInt(L"height");
            s.Segments.push_back(sz);
        }
    }
    s.PreviousPrimaryDevice = root.GetString(L"previousPrimaryDevice");
    s.PreviousPrimaryFriendlyName = root.GetString(L"previousPrimaryFriendlyName");
    s.AppliedPrimaryDevice = root.GetString(L"appliedPrimaryDevice");
    s.HidePhysicalDisplay = root.GetBool(L"hidePhysicalDisplay");
    s.SpecializedDevice = root.GetString(L"specializedDevice");
    s.SpecializationHashRecipe = root.GetString(L"specializationHashRecipe");
    if (auto* hashes = root.Find(L"specializationHashes"); hashes && hashes->Type == JsonType::Object) {
        for (const auto& kv : hashes->Object) {
            if (kv.second.Type == JsonType::String)
                s.SpecializationHashes[kv.first] = kv.second.String;
        }
    }
    if (auto* utc = root.Find(L"lastAppliedUtc"); utc && utc->Type == JsonType::String && !utc->String.empty())
        s.LastAppliedUtc = utc->String;
    s.MinimizeToTrayOnClose = root.GetBool(L"minimizeToTrayOnClose", true);
    s.StartWithWindows = root.GetBool(L"startWithWindows");
    s.ApplyOnStartup = root.GetBool(L"applyOnStartup");
    s.SplitActiveOnExit = root.GetBool(L"splitActiveOnExit");
    return s;
}

static JsonValue ToJson(const AppSettings& s) {
    JsonValue root = JsonValue::Obj();
    root.Set(L"physicalDevice", JsonValue::FromString(s.PhysicalDevice));
    root.Set(L"physicalFriendlyName", JsonValue::FromString(s.PhysicalFriendlyName));
    root.Set(L"layout", JsonValue::FromInt((int)s.Layout));
    JsonValue segs = JsonValue::Arr();
    for (const auto& z : s.Segments) {
        JsonValue item = JsonValue::Obj();
        item.Set(L"width", JsonValue::FromInt(z.Width));
        item.Set(L"height", JsonValue::FromInt(z.Height));
        segs.Array.push_back(std::move(item));
    }
    root.Set(L"segments", std::move(segs));
    root.Set(L"previousPrimaryDevice", JsonValue::FromString(s.PreviousPrimaryDevice));
    root.Set(L"previousPrimaryFriendlyName", JsonValue::FromString(s.PreviousPrimaryFriendlyName));
    root.Set(L"appliedPrimaryDevice", JsonValue::FromString(s.AppliedPrimaryDevice));
    root.Set(L"hidePhysicalDisplay", JsonValue::FromBool(s.HidePhysicalDisplay));
    root.Set(L"specializedDevice", JsonValue::FromString(s.SpecializedDevice));
    root.Set(L"specializationHashRecipe", JsonValue::FromString(s.SpecializationHashRecipe));
    JsonValue hashes = JsonValue::Obj();
    for (const auto& kv : s.SpecializationHashes)
        hashes.Set(kv.first.c_str(), JsonValue::FromString(kv.second));
    root.Set(L"specializationHashes", std::move(hashes));
    if (s.LastAppliedUtc)
        root.Set(L"lastAppliedUtc", JsonValue::FromString(*s.LastAppliedUtc));
    else
        root.Set(L"lastAppliedUtc", JsonValue::Null());
    root.Set(L"minimizeToTrayOnClose", JsonValue::FromBool(s.MinimizeToTrayOnClose));
    root.Set(L"startWithWindows", JsonValue::FromBool(s.StartWithWindows));
    root.Set(L"applyOnStartup", JsonValue::FromBool(s.ApplyOnStartup));
    root.Set(L"splitActiveOnExit", JsonValue::FromBool(s.SplitActiveOnExit));
    return root;
}

std::optional<AppSettings> SettingsStore::Load() {
    try {
        string text;
        if (!ReadUtf8File(FilePath(), text)) return std::nullopt;
        JsonValue root;
        if (!JsonParse(text, root, nullptr)) return std::nullopt;
        return FromJson(root);
    } catch (...) {
        return std::nullopt;
    }
}

void SettingsStore::Save(const AppSettings& settings) {
    EnsureDirectory(Directory());
    string json = JsonStringify(ToJson(settings), true);
    if (!WriteUtf8File(FilePath(), json))
        throw std::runtime_error("Could not write settings.json");
}

string CompositorConfigToJson(const CompositorConfig& cfg) {
    JsonValue root = JsonValue::Obj();
    root.Set(L"physicalDevice", JsonValue::FromString(cfg.PhysicalDevice));
    root.Set(L"refreshMillihertz", JsonValue::FromNumber((double)cfg.RefreshMillihertz));
    JsonValue segs = JsonValue::Arr();
    for (const auto& s : cfg.Segments) {
        JsonValue item = JsonValue::Obj();
        item.Set(L"virtualDevice", JsonValue::FromString(s.VirtualDevice));
        item.Set(L"width", JsonValue::FromInt(s.Width));
        item.Set(L"height", JsonValue::FromInt(s.Height));
        JsonValue r = JsonValue::Obj();
        r.Set(L"x", JsonValue::FromInt(s.PhysRect.X));
        r.Set(L"y", JsonValue::FromInt(s.PhysRect.Y));
        r.Set(L"w", JsonValue::FromInt(s.PhysRect.W));
        r.Set(L"h", JsonValue::FromInt(s.PhysRect.H));
        item.Set(L"physRect", std::move(r));
        segs.Array.push_back(std::move(item));
    }
    root.Set(L"segments", std::move(segs));
    return JsonStringify(root, true);
}

} // namespace ds
