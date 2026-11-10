#include "util/JsonIo.h"
#include <fstream>
#include <sstream>
#include <shlobj.h>

namespace ds {
namespace {

struct Parser {
    const string& s;
    size_t i = 0;
    wstring err;

    void Skip() {
        while (i < s.size() && (unsigned char)s[i] <= 32) ++i;
    }
    bool Peek(char c) { Skip(); return i < s.size() && s[i] == c; }
    bool Eat(char c) {
        Skip();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
    bool ParseValue(JsonValue& v);
    bool ParseString(wstring& out);
    bool ParseNumber(double& out);
};

bool Parser::ParseString(wstring& out) {
    if (!Eat('"')) { err = L"expected string"; return false; }
    string raw;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') break;
        if (c == '\\') {
            if (i >= s.size()) { err = L"unterminated escape"; return false; }
            char e = s[i++];
            switch (e) {
            case '"': case '\\': case '/': raw.push_back(e); break;
            case 'b': raw.push_back('\b'); break;
            case 'f': raw.push_back('\f'); break;
            case 'n': raw.push_back('\n'); break;
            case 'r': raw.push_back('\r'); break;
            case 't': raw.push_back('\t'); break;
            case 'u': {
                if (i + 4 > s.size()) { err = L"bad unicode escape"; return false; }
                unsigned code = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = s[i++];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code += h - '0';
                    else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
                    else { err = L"bad unicode escape"; return false; }
                }
                // UTF-8 encode BMP code point
                if (code < 0x80) raw.push_back((char)code);
                else if (code < 0x800) {
                    raw.push_back((char)(0xC0 | (code >> 6)));
                    raw.push_back((char)(0x80 | (code & 0x3F)));
                } else {
                    raw.push_back((char)(0xE0 | (code >> 12)));
                    raw.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                    raw.push_back((char)(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: raw.push_back(e); break;
            }
        } else {
            raw.push_back(c);
        }
    }
    out = Widen(raw);
    return true;
}

bool Parser::ParseNumber(double& out) {
    Skip();
    size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
    if (i < s.size() && s[i] == '.') { ++i; while (i < s.size() && isdigit((unsigned char)s[i])) ++i; }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
    }
    if (start == i) { err = L"expected number"; return false; }
    try { out = std::stod(s.substr(start, i - start)); }
    catch (...) { err = L"invalid number"; return false; }
    return true;
}

bool Parser::ParseValue(JsonValue& v) {
    Skip();
    if (i >= s.size()) { err = L"unexpected end"; return false; }
    if (s[i] == '"') {
        v.Type = JsonType::String;
        return ParseString(v.String);
    }
    if (s[i] == '{') {
        ++i;
        v.Type = JsonType::Object;
        v.Object.clear();
        Skip();
        if (Eat('}')) return true;
        for (;;) {
            wstring key;
            if (!ParseString(key)) return false;
            if (!Eat(':')) { err = L"expected ':'"; return false; }
            JsonValue child;
            if (!ParseValue(child)) return false;
            v.Object.emplace_back(std::move(key), std::move(child));
            Skip();
            if (Eat('}')) return true;
            if (!Eat(',')) { err = L"expected ',' or '}'"; return false; }
        }
    }
    if (s[i] == '[') {
        ++i;
        v.Type = JsonType::Array;
        v.Array.clear();
        Skip();
        if (Eat(']')) return true;
        for (;;) {
            JsonValue child;
            if (!ParseValue(child)) return false;
            v.Array.push_back(std::move(child));
            Skip();
            if (Eat(']')) return true;
            if (!Eat(',')) { err = L"expected ',' or ']'"; return false; }
        }
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; v = JsonValue::FromBool(true); return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; v = JsonValue::FromBool(false); return true; }
    if (s.compare(i, 4, "null") == 0) { i += 4; v = JsonValue::Null(); return true; }
    v.Type = JsonType::Number;
    return ParseNumber(v.Number);
}

void EscapeUtf8(const string& in, string& out) {
    out.push_back('"');
    for (unsigned char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back((char)c);
            }
        }
    }
    out.push_back('"');
}

void Stringify(const JsonValue& v, string& out, bool pretty, int indent) {
    auto pad = [&](int n) { if (pretty) out.append(n * 2, ' '); };
    switch (v.Type) {
    case JsonType::Null: out += "null"; break;
    case JsonType::Bool: out += v.Bool ? "true" : "false"; break;
    case JsonType::Number: {
        if (std::floor(v.Number) == v.Number && std::fabs(v.Number) < 1e15) {
            char buf[64];
            sprintf_s(buf, "%.0f", v.Number);
            out += buf;
        } else {
            char buf[64];
            sprintf_s(buf, "%.17g", v.Number);
            out += buf;
        }
        break;
    }
    case JsonType::String: EscapeUtf8(Narrow(v.String), out); break;
    case JsonType::Array: {
        out += pretty ? "[\n" : "[";
        for (size_t i = 0; i < v.Array.size(); ++i) {
            if (pretty) pad(indent + 1);
            Stringify(v.Array[i], out, pretty, indent + 1);
            if (i + 1 < v.Array.size()) out += pretty ? ",\n" : ",";
            else if (pretty) out += "\n";
        }
        if (pretty) pad(indent);
        out += "]";
        break;
    }
    case JsonType::Object: {
        out += pretty ? "{\n" : "{";
        for (size_t i = 0; i < v.Object.size(); ++i) {
            if (pretty) pad(indent + 1);
            EscapeUtf8(Narrow(v.Object[i].first), out);
            out += pretty ? ": " : ":";
            Stringify(v.Object[i].second, out, pretty, indent + 1);
            if (i + 1 < v.Object.size()) out += pretty ? ",\n" : ",";
            else if (pretty) out += "\n";
        }
        if (pretty) pad(indent);
        out += "}";
        break;
    }
    }
}

wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) {
        out = p;
        CoTaskMemFree(p);
    }
    return out;
}

} // namespace

JsonValue* JsonValue::Find(const wchar_t* key) {
    for (auto& kv : Object) if (kv.first == key) return &kv.second;
    return nullptr;
}
const JsonValue* JsonValue::Find(const wchar_t* key) const {
    for (const auto& kv : Object) if (kv.first == key) return &kv.second;
    return nullptr;
}
wstring JsonValue::GetString(const wchar_t* key, const wstring& def) const {
    auto* v = Find(key);
    if (!v) return def;
    if (v->Type == JsonType::String) return v->String;
    return def;
}
int JsonValue::GetInt(const wchar_t* key, int def) const {
    auto* v = Find(key);
    if (!v) return def;
    if (v->Type == JsonType::Number) return (int)std::lround(v->Number);
    return def;
}
double JsonValue::GetNumber(const wchar_t* key, double def) const {
    auto* v = Find(key);
    if (!v || v->Type != JsonType::Number) return def;
    return v->Number;
}
bool JsonValue::GetBool(const wchar_t* key, bool def) const {
    auto* v = Find(key);
    if (!v) return def;
    if (v->Type == JsonType::Bool) return v->Bool;
    return def;
}
void JsonValue::Set(const wchar_t* key, JsonValue v) {
    Type = JsonType::Object;
    for (auto& kv : Object) {
        if (kv.first == key) { kv.second = std::move(v); return; }
    }
    Object.emplace_back(key, std::move(v));
}

bool JsonParse(const string& utf8, JsonValue& out, wstring* error) {
    Parser p{ utf8 };
    if (!p.ParseValue(out)) {
        if (error) *error = p.err;
        return false;
    }
    return true;
}

string JsonStringify(const JsonValue& v, bool pretty) {
    string out;
    Stringify(v, out, pretty, 0);
    if (pretty) out += "\n";
    return out;
}

bool ReadUtf8File(const wstring& path, string& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t r = fread(out.data(), 1, out.size(), f);
    fclose(f);
    out.resize(r);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

bool WriteUtf8File(const wstring& path, const string& utf8) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    size_t w = fwrite(utf8.data(), 1, utf8.size(), f);
    fclose(f);
    return w == utf8.size();
}

bool EnsureDirectory(const wstring& dir) {
    if (dir.empty()) return false;
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

wstring GetAppDataDir() {
    return KnownFolder(FOLDERID_RoamingAppData) + L"\\DesktopSplitter";
}
wstring GetProgramDataDir() {
    return KnownFolder(FOLDERID_ProgramData) + L"\\DesktopSplitter";
}
wstring SettingsFilePath() { return GetAppDataDir() + L"\\settings.json"; }
wstring CompositorConfigPath() { return GetProgramDataDir() + L"\\config.json"; }

} // namespace ds
