// ofxhost - minimal streaming JSON writer (no dependencies).
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ofxhost {

class Json {
public:
    std::string str() const { return out_; }

    Json& beginObject() { sep(); out_ += '{'; stack_.push_back(true); first_ = true; return *this; }
    Json& endObject() { out_ += '}'; stack_.pop_back(); first_ = false; return *this; }
    Json& beginArray() { sep(); out_ += '['; stack_.push_back(false); first_ = true; return *this; }
    Json& endArray() { out_ += ']'; stack_.pop_back(); first_ = false; return *this; }
    Json& key(const std::string& k) {
        sep();
        out_ += quote(k);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }
    Json& value(const std::string& s) { sep(); out_ += quote(s); return *this; }
    Json& value(const char* s) { return value(std::string(s ? s : "")); }
    Json& value(bool b) { sep(); out_ += b ? "true" : "false"; return *this; }
    Json& value(int v) { sep(); out_ += std::to_string(v); return *this; }
    Json& value(long long v) { sep(); out_ += std::to_string(v); return *this; }
    Json& value(unsigned long long v) { sep(); out_ += std::to_string(v); return *this; }
    Json& value(double v) {
        sep();
        if (std::isnan(v) || std::isinf(v)) { out_ += "null"; return *this; }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.10g", v);
        out_ += buf;
        return *this;
    }
    Json& null() { sep(); out_ += "null"; return *this; }
    template <class T> Json& kv(const std::string& k, const T& v) { key(k); value(v); return *this; }
    Json& kv(const std::string& k, const std::vector<std::string>& v) {
        key(k); beginArray(); for (auto& s : v) value(s); endArray(); return *this;
    }
    Json& kv(const std::string& k, const std::vector<double>& v) {
        key(k); beginArray(); for (double d : v) value(d); endArray(); return *this;
    }
    Json& raw(const std::string& r) { sep(); out_ += r; return *this; }

    static std::string quote(const std::string& s) {
        std::string r = "\"";
        for (unsigned char c : s) {
            switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
                else r += char(c);
            }
        }
        return r + "\"";
    }

private:
    void sep() {
        if (afterKey_) { afterKey_ = false; first_ = false; return; }
        if (!stack_.empty() && !first_) out_ += ',';
        first_ = false;
    }
    std::string out_;
    std::vector<bool> stack_;
    bool first_ = true;
    bool afterKey_ = false;
};

} // namespace ofxhost
