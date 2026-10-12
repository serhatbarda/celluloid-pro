// Reafilm Pro - user presets (see UserPresets.h). A small self-contained JSON reader / writer: the preset
// files are tiny and flat, and numbers are read / written in the classic "C" locale whatever the host set.
#include "UserPresets.h"
#include "Platform.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace cel { namespace ofxp {

namespace {

// ---- JSON value (only what a preset needs; arrays are parsed and kept as "other") ----
struct JValue {
    enum Kind { Null, Bool, Number, String, Object, Other } kind = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<std::pair<std::string, JValue>> obj;
};

struct Parser {
    const std::string& t;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& text) : t(text) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
    }
    bool fail(const std::string& what) {
        if (err.empty()) err = what + " at byte " + std::to_string(i);
        return false;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o.push_back(char(cp));
        else if (cp < 0x800) { o.push_back(char(0xC0 | (cp >> 6))); o.push_back(char(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            o.push_back(char(0xE0 | (cp >> 12))); o.push_back(char(0x80 | ((cp >> 6) & 0x3F))); o.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            o.push_back(char(0xF0 | (cp >> 18))); o.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            o.push_back(char(0x80 | ((cp >> 6) & 0x3F))); o.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > t.size()) return fail("truncated \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool str(std::string& o) {
        if (i >= t.size() || t[i] != '"') return fail("expected a string");
        ++i;
        while (i < t.size()) {
            const char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') { o.push_back(c); continue; }
            if (i >= t.size()) break;
            const char e = t[i++];
            switch (e) {
            case '"': o.push_back('"'); break;
            case '\\': o.push_back('\\'); break;
            case '/': o.push_back('/'); break;
            case 'b': o.push_back('\b'); break;
            case 'f': o.push_back('\f'); break;
            case 'n': o.push_back('\n'); break;
            case 'r': o.push_back('\r'); break;
            case 't': o.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    i += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(o, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool number(double& v) {
        const size_t a = i;
        if (i < t.size() && (t[i] == '-' || t[i] == '+')) ++i;
        while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '.' || t[i] == 'e' ||
                                t[i] == 'E' || t[i] == '-' || t[i] == '+'))
            ++i;
        std::istringstream ss(t.substr(a, i - a));
        ss.imbue(std::locale::classic());
        ss >> v;
        if (ss.fail() || !ss.eof() || i == a) return fail("bad number");
        return true;
    }
    bool value(JValue& v, int depth) {
        if (depth > 32) return fail("nested too deeply");
        ws();
        if (i >= t.size()) return fail("unexpected end");
        const char c = t[i];
        if (c == '{') {
            v.kind = JValue::Object;
            ++i;
            ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= t.size() || t[i] != ':') return fail("expected ':'");
                ++i;
                JValue m;
                if (!value(m, depth + 1)) return false;
                v.obj.emplace_back(std::move(k), std::move(m));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = JValue::Other;
            ++i;
            ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            for (;;) {
                JValue m;
                if (!value(m, depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.kind = JValue::String; return str(v.s); }
        if (t.compare(i, 4, "true") == 0) { v.kind = JValue::Bool; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.kind = JValue::Bool; v.b = false; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { v.kind = JValue::Null; i += 4; return true; }
        v.kind = JValue::Number;
        return number(v.n);
    }
};

void escape(std::ostringstream& o, const std::string& s) {
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            if (c < 0x20) {
                static const char* hx = "0123456789abcdef";
                o << "\\u00" << hx[c >> 4] << hx[c & 15];
            } else {
                o << char(c);
            }
        }
    }
    o << '"';
}

// shortest of %.15g / %.17g that reads back exactly (classic locale)
std::string numText(double v) {
    if (!std::isfinite(v)) return "0";
    for (int prec : {15, 17}) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(prec);
        o << v;
        std::istringstream back(o.str());
        back.imbue(std::locale::classic());
        double r = 0;
        back >> r;
        if (r == v || prec == 17) return o.str();
    }
    return "0";
}

} // namespace

bool userPresetStores(const ParamDef& d) {
    if (!d.persistent) return false;
    if (d.type != PType::Double && d.type != PType::Int && d.type != PType::Bool && d.type != PType::Choice &&
        d.type != PType::String)
        return false;
    if (d.group == id::gLicense) return false;
    return d.id != id::lutPath && d.id != id::userPresetName && d.id != id::falseColourEnable &&
           !isDiagnosticViewParam(d.id);
}

std::string userPresetToJson(const std::string& name, const std::string& pluginVersion,
                             const std::vector<UserPresetValue>& values) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "{\n  \"format\": \"" << kUserPresetFormat << "\",\n  \"version\": " << kUserPresetVersion << ",\n  \"plugin\": ";
    escape(o, pluginVersion);
    o << ",\n  \"name\": ";
    escape(o, name);
    o << ",\n  \"params\": {";
    bool first = true;
    for (const UserPresetValue& v : values) {
        const ParamDef* d = findParam(v.param);
        if (!d || !userPresetStores(*d)) continue;
        o << (first ? "\n    " : ",\n    ");
        first = false;
        escape(o, d->id);
        o << ": ";
        switch (d->type) {
        case PType::Bool: o << (v.num != 0.0 ? "true" : "false"); break;
        case PType::Choice: {
            const long k = std::lround(v.num);
            if (k >= 0 && k < long(d->options.size())) escape(o, d->options[size_t(k)]);
            else o << numText(double(k));
            break;
        }
        case PType::String: escape(o, v.text); break;
        case PType::Int: o << numText(double(std::lround(v.num))); break;
        default: o << numText(v.num); break;
        }
    }
    o << "\n  }\n}\n";
    return o.str();
}

bool userPresetFromJson(const std::string& json, std::vector<UserPresetValue>& out, std::string* name,
                        std::vector<std::string>* notes, std::string* err) {
    out.clear();
    auto note = [&](const std::string& s) { if (notes) notes->push_back(s); };
    Parser p(json);
    JValue root;
    // a UTF-8 byte order mark is tolerated
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF && static_cast<unsigned char>(json[1]) == 0xBB &&
        static_cast<unsigned char>(json[2]) == 0xBF)
        p.i = 3;
    if (!p.value(root, 0)) {
        if (err) *err = "not valid JSON (" + p.err + ")";
        return false;
    }
    p.ws();
    if (p.i != json.size()) note("text after the preset object was ignored");
    if (root.kind != JValue::Object) {
        if (err) *err = "not a Reafilm preset (no JSON object)";
        return false;
    }
    const JValue* params = nullptr;
    bool formatOk = false;
    for (const auto& kv : root.obj) {
        if (kv.first == "format") formatOk = kv.second.kind == JValue::String && kv.second.s == kUserPresetFormat;
        else if (kv.first == "params" && kv.second.kind == JValue::Object) params = &kv.second;
        else if (kv.first == "name" && kv.second.kind == JValue::String) { if (name) *name = kv.second.s; }
        else if (kv.first == "version" && kv.second.kind == JValue::Number) {
            if (kv.second.n > double(kUserPresetVersion))
                note("preset file version " + numText(kv.second.n) + " is newer than this plug-in (" +
                     std::to_string(kUserPresetVersion) + "); loading what this version knows");
        } else if (kv.first != "plugin" && kv.first != "version") {
            note("unknown entry \"" + kv.first + "\" ignored");
        }
    }
    if (!formatOk || !params) {
        if (err) *err = "not a Reafilm preset (\"format\": \"" + std::string(kUserPresetFormat) + "\" and a \"params\" object are required)";
        return false;
    }
    std::map<std::string, const JValue*> byId;
    for (const auto& kv : params->obj) {
        const ParamDef* d = findParam(kv.first);
        if (!d || !userPresetStores(*d)) {
            note("unknown parameter \"" + kv.first + "\" ignored");
            continue;
        }
        byId[kv.first] = &kv.second;
    }
    int missing = 0;
    for (const ParamDef& d : paramDefs()) {
        if (!userPresetStores(d)) continue;
        UserPresetValue v;
        v.param = d.id;
        v.num = numericDefault(d);
        v.text = d.defString;
        auto it = byId.find(d.id);
        if (it == byId.end()) {
            ++missing;
            out.push_back(v);
            continue;
        }
        const JValue& j = *it->second;
        bool ok = true;
        switch (d.type) {
        case PType::String:
            if (j.kind == JValue::String) v.text = j.s;
            else ok = false;
            break;
        case PType::Bool:
            if (j.kind == JValue::Bool) v.num = j.b ? 1.0 : 0.0;
            else if (j.kind == JValue::Number) v.num = j.n != 0.0 ? 1.0 : 0.0;
            else ok = false;
            break;
        case PType::Choice: {
            int k = -1;
            if (j.kind == JValue::String) k = choiceIndex(d, j.s);
            else if (j.kind == JValue::Number && j.n >= 0 && j.n < double(d.options.size())) k = int(j.n);
            if (k >= 0) v.num = double(k);
            else ok = false;
            break;
        }
        default:   // Double / Int
            if (j.kind == JValue::Number && std::isfinite(j.n)) {
                v.num = std::min(std::max(j.n, d.min), d.max);
                if (d.type == PType::Int) v.num = double(std::lround(v.num));
                if (v.num != j.n) note(d.id + ": value clamped to the parameter range");
            } else {
                ok = false;
            }
        }
        if (!ok) note(d.id + ": invalid value, the default is used");
        out.push_back(v);
    }
    if (missing > 0)
        note(std::to_string(missing) + " setting(s) not in the file (older preset) are at their defaults");
    return true;
}

std::string userPresetFileName(const std::string& name) { return fileComponent(name) + ".json"; }

std::string findUserPresetFile(const std::string& name) {
    const std::string file = userPresetFileName(name);
    for (const std::string& dir : {userPresetDirectory(), legacyUserPresetDirectory()}) {
        if (dir.empty()) continue;
        const std::string path = joinPath(dir, file);
        if (fileExists(path)) return path;
    }
    return std::string();
}

std::vector<std::string> listUserPresets() {
    std::set<std::string> names;
    for (const std::string& dir : {userPresetDirectory(), legacyUserPresetDirectory()}) {
        if (dir.empty()) continue;
        for (const std::string& f : listFiles(dir, ".json")) names.insert(f.substr(0, f.size() - 5));
    }
    return std::vector<std::string>(names.begin(), names.end());
}

}} // namespace cel::ofxp
