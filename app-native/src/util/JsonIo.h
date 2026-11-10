#pragma once
#include "Common.h"

// Minimal JSON object/array used for settings.json and config.json.
// Unknown fields are ignored on load so existing user files keep working.
namespace ds {

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType Type = JsonType::Null;
    bool Bool = false;
    double Number = 0;
    wstring String;
    std::vector<JsonValue> Array;
    std::vector<std::pair<wstring, JsonValue>> Object;

    static JsonValue Null() { return {}; }
    static JsonValue FromBool(bool v) { JsonValue j; j.Type = JsonType::Bool; j.Bool = v; return j; }
    static JsonValue FromNumber(double v) { JsonValue j; j.Type = JsonType::Number; j.Number = v; return j; }
    static JsonValue FromInt(int v) { return FromNumber((double)v); }
    static JsonValue FromString(const wstring& s) { JsonValue j; j.Type = JsonType::String; j.String = s; return j; }
    static JsonValue Arr() { JsonValue j; j.Type = JsonType::Array; return j; }
    static JsonValue Obj() { JsonValue j; j.Type = JsonType::Object; return j; }

    JsonValue* Find(const wchar_t* key);
    const JsonValue* Find(const wchar_t* key) const;
    wstring GetString(const wchar_t* key, const wstring& def = L"") const;
    int GetInt(const wchar_t* key, int def = 0) const;
    double GetNumber(const wchar_t* key, double def = 0) const;
    bool GetBool(const wchar_t* key, bool def = false) const;
    void Set(const wchar_t* key, JsonValue v);
};

bool JsonParse(const string& utf8, JsonValue& out, wstring* error = nullptr);
string JsonStringify(const JsonValue& v, bool pretty = true);

bool ReadUtf8File(const wstring& path, string& out);
bool WriteUtf8File(const wstring& path, const string& utf8);
bool EnsureDirectory(const wstring& dir);

wstring GetAppDataDir();      // %APPDATA%\DesktopSplitter
wstring GetProgramDataDir();  // %ProgramData%\DesktopSplitter
wstring SettingsFilePath();
wstring CompositorConfigPath();

} // namespace ds
