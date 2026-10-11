// Reafilm Pro - platform helpers for the OFX shell.
#include "Platform.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>
#  include <knownfolders.h>
#else
#  include <pwd.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace cel { namespace ofxp {

namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
fs::path toPath(const std::string& utf8) { return fs::path(widen(utf8)); }
std::string fromPath(const fs::path& p) { return narrow(p.wstring()); }
const char kSep = '\\';
#else
fs::path toPath(const std::string& utf8) { return fs::path(utf8); }
std::string fromPath(const fs::path& p) { return p.string(); }
const char kSep = '/';
#endif

bool isSep(char c) { return c == '/' || c == '\\'; }

std::string homeDirectory() {
#if defined(_WIN32)
    std::string h = envVar("USERPROFILE");
    if (!h.empty()) return h;
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p)) && p) out = narrow(p);
    if (p) CoTaskMemFree(p);
    return out;
#else
    std::string h = envVar("HOME");
    if (!h.empty()) return h;
    if (const passwd* pw = getpwuid(getuid())) {
        if (pw->pw_dir) return pw->pw_dir;
    }
    return std::string();
#endif
}

} // namespace

std::string envVar(const char* name) {
#if defined(_WIN32)
    // _wgetenv keeps non-ASCII values intact
    const wchar_t* v = _wgetenv(widen(name).c_str());
    return v ? narrow(v) : std::string();
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

std::string productEnv(const char* suffix) {
    const std::string v = envVar(("REAFILM_" + std::string(suffix)).c_str());
    return v.empty() ? envVar(("CELLULOID_" + std::string(suffix)).c_str()) : v;
}

std::string documentsDirectory() {
#if defined(_WIN32)
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &p)) && p) out = narrow(p);
    if (p) CoTaskMemFree(p);
    if (!out.empty()) return out;
    std::string home = homeDirectory();
    return home.empty() ? std::string(".") : joinPath(home, "Documents");
#else
#  if !defined(__APPLE__)
    // XDG user dirs (Linux): honour an explicit XDG_DOCUMENTS_DIR
    std::string xdg = envVar("XDG_DOCUMENTS_DIR");
    if (!xdg.empty()) return normalizeUserPath(xdg);
#  endif
    std::string home = homeDirectory();
    return home.empty() ? std::string(".") : joinPath(home, "Documents");
#endif
}

std::string defaultLutDirectory() {
    std::string d = joinPath(documentsDirectory(), "Reafilm LUTs");
    d.push_back(kSep);
    return d;
}

std::string normalizeUserPath(const std::string& in) {
    size_t a = 0, b = in.size();
    auto trimmable = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\''; };
    while (a < b && trimmable(in[a])) ++a;
    while (b > a && trimmable(in[b - 1])) --b;
    std::string s = in.substr(a, b - a);
    if (!s.empty() && s[0] == '~' && (s.size() == 1 || isSep(s[1]))) {
        std::string home = homeDirectory();
        if (!home.empty()) s = home + s.substr(1);
    }
    return s;
}

std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    std::string d = dir;
    while (d.size() > 1 && isSep(d.back())) d.pop_back();
    if (isSep(d.back())) return d + file;    // root "/"
    return d + kSep + file;
}

bool ensureDirectory(const std::string& dir, std::string* err) {
    std::error_code ec;
    fs::path p = toPath(dir);
    if (fs::is_directory(p, ec)) return true;
    fs::create_directories(p, ec);
    std::error_code ec2;
    if (fs::is_directory(p, ec2)) return true;
    if (err) *err = "cannot create folder \"" + fromPath(p) + "\"" + (ec ? (": " + ec.message()) : std::string());
    return false;
}

std::string fileComponent(const std::string& label) {
    std::string out;
    bool pendingDash = false;
    for (unsigned char c : label) {
        if (std::isalnum(c) && c < 128) {
            if (pendingDash && !out.empty()) out.push_back('-');
            pendingDash = false;
            out.push_back(char(c));
        } else {
            pendingDash = true;
        }
    }
    return out.empty() ? std::string("Untitled") : out;
}

}} // namespace cel::ofxp
