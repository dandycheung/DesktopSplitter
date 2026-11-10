#pragma once
#include "models/Types.h"
#include <unordered_map>

namespace ds {

struct CiHash {
    size_t operator()(const wstring& s) const {
        size_t h = 1469598103934665603ull;
        for (wchar_t c : s) {
            h ^= (size_t)towlower(c);
            h *= 1099511628211ull;
        }
        return h;
    }
};
struct CiEq {
    bool operator()(const wstring& a, const wstring& b) const {
        return EqualsIgnoreCase(a, b);
    }
};

using EdidMap = std::unordered_map<wstring, EdidInfo, CiHash, CiEq>;

class EdidReader {
public:
    static EdidMap BuildMap();
    static bool TryRead(const wstring& monitorDevicePath, EdidInfo& edid);
    static bool TryLookup(const EdidMap& map, const wstring& monitorDevicePath, EdidInfo& edid);
    static wstring DeviceInstanceIdFromInterfacePath(const wstring& interfacePath);
    static std::optional<EdidInfo> Parse(const uint8_t* blob, size_t length);
};

} // namespace ds
