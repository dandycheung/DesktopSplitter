#pragma once
#include "models/Types.h"
#include "interop/MonitorSpecialization.h"

namespace ds {

enum class CheckState { Unknown, Ok, No, Info };

struct StatusRow {
    wstring Label;
    CheckState State = CheckState::Unknown;
    wstring Detail;
    wstring Glyph() const {
        switch (State) {
        case CheckState::Ok: return L"\u2713"; // ✓
        case CheckState::No: return L"\u2717"; // ✗
        case CheckState::Info: return L"\u2013"; // en dash
        default: return L"?";
        }
    }
};

struct HidingSnapshot {
    StatusRow SystemSupport, MonitorSupport, CurrentlySpecialized, Intent;
    bool SplitActive = false;
    bool Available = false;
    bool EnabledNow = false;
    bool IntentOn = false;
    std::vector<StatusRow> Rows() const {
        return { SystemSupport, MonitorSupport, CurrentlySpecialized, Intent };
    }
};

class HidingStatus {
public:
    static HidingSnapshot Query(const MonitorInfo* selected);
    static bool IsSplitActive();
    static bool ReadIntent();

    struct IntentWriteResult {
        bool Success = false;
        bool MachineWideWritten = false;
        wstring Message;
    };
    static IntentWriteResult WriteIntent(bool enabled);
};

} // namespace ds
