// Celluloid Pro - license persistence and the cached, thread-safe current() status.
//
// Sources, in order of precedence (the first VALID paid-edition key wins; a valid Trial-edition
// evaluation key counts only when no source holds a paid key, so a leftover evaluation key in the
// user file never re-watermarks a machine licensed by the environment or machine-wide):
//   1. per-user key file   macOS   ~/Library/Application Support/Celluloid/license.key
//                          Windows %APPDATA%\Celluloid\license.key   (FOLDERID_RoamingAppData)
//                          Linux   $XDG_CONFIG_HOME/celluloid/license.key or ~/.config/celluloid/...
//   2. environment variable CELLULOID_LICENSE_KEY (render nodes, containers)
//   3. machine-wide file   macOS   /Library/Application Support/Celluloid/license.key
//                          Windows %PROGRAMDATA%\Celluloid\license.key  (FOLDERID_ProgramData)
//                          Linux   /etc/celluloid/license.key
// If no source is valid, the status explains the first source that is present but invalid
// (e.g. "expired"), else it is the trial status.
//
// Key files hold the key text; lines whose first non-blank character is '#' are comments
// (admins may annotate machine-wide files). Files larger than 64 KiB are rejected.
//
// Caching: current() is called from render threads. It returns the cached status and only
// re-reads the (tiny) files when at least kRecheckMs have passed; the file I/O and the signature
// check happen OUTSIDE the mutex so render threads never wait on a slow (network) home folder.
// The one exception is the very first call of the process, which loads synchronously (there is
// no status to return yet). Invalidation (saveKey/removeKey/invalidateCache) keeps the previous
// status and only marks it due, so the reload also runs outside the mutex; saveKey()/removeKey()
// reload synchronously on the calling (UI) thread before they return, so the new status is
// visible at once. The cached result is also re-verified every 60 s so a time-limited key
// expires on time. Nothing throws across the API.
#include "License.h"
#include "KeyFormat.h"
#include "LicenseInternal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <initguid.h>      // define FOLDERID_* here (selectany) - no uuid.lib dependency
  #include <knownfolders.h>
  #include <shlobj.h>
  #if defined(_MSC_VER)
    #pragma comment(lib, "shell32.lib")
    #pragma comment(lib, "ole32.lib")
  #endif
#else
  #include <pwd.h>
  #include <unistd.h>
  #include <sys/types.h>
#endif

namespace fs = std::filesystem;

namespace cel { namespace lic {

namespace {

constexpr uintmax_t kMaxKeyFileBytes = 64 * 1024;
constexpr int64_t   kReverifySeconds = 60;

std::string pathToUtf8(const fs::path& p) {
#if defined(__cpp_char8_t)
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

fs::path pathFromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

// ---- platform locations ---------------------------------------------------------------------
#if defined(_WIN32)
fs::path knownFolder(REFKNOWNFOLDERID id, const wchar_t* envFallback) {
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) r = fs::path(p);
    if (p) CoTaskMemFree(p);
    if (r.empty()) {
        wchar_t buf[MAX_PATH * 2];
        const DWORD n = GetEnvironmentVariableW(envFallback, buf, DWORD(sizeof(buf) / sizeof(buf[0])));
        if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) r = fs::path(std::wstring(buf, n));
    }
    return r;
}
fs::path defaultUserFile() {
    fs::path base = knownFolder(FOLDERID_RoamingAppData, L"APPDATA");
    return base.empty() ? fs::path() : base / L"Celluloid" / L"license.key";
}
fs::path defaultMachineFile() {
    fs::path base = knownFolder(FOLDERID_ProgramData, L"PROGRAMDATA");
    return base.empty() ? fs::path() : base / L"Celluloid" / L"license.key";
}
#else
fs::path homeDir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return fs::path(h);
    struct passwd pw, *res = nullptr;
    char buf[4096];
    if (getpwuid_r(getuid(), &pw, buf, sizeof(buf), &res) == 0 && res && res->pw_dir && *res->pw_dir)
        return fs::path(res->pw_dir);
    return fs::path();
}
  #if defined(__APPLE__)
fs::path defaultUserFile() {
    fs::path h = homeDir();
    return h.empty() ? fs::path() : h / "Library" / "Application Support" / "Celluloid" / "license.key";
}
fs::path defaultMachineFile() { return fs::path("/Library/Application Support/Celluloid/license.key"); }
  #else
fs::path defaultUserFile() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (x && x[0] == '/') return fs::path(x) / "celluloid" / "license.key";   // XDG: must be absolute
    fs::path h = homeDir();
    return h.empty() ? fs::path() : h / ".config" / "celluloid" / "license.key";
}
fs::path defaultMachineFile() { return fs::path("/etc/celluloid/license.key"); }
  #endif
#endif

// ---- state ----------------------------------------------------------------------------------
struct State {
    std::mutex m;
    bool        have = false;
    LicenseInfo info;
    std::chrono::steady_clock::time_point lastCheck{};
    int64_t     lastVerifyNow = 0;
    bool        refreshing = false;
    uint64_t    generation = 0;
    // test overrides
    bool        overridePaths = false;
    fs::path    userOverride, machineOverride;
    int         recheckMs = 1500;
    int64_t     fixedNow = 0;
};

State& state() {
    static State s;   // thread-safe initialisation (C++11 magic statics)
    return s;
}

int64_t clockNow(const State& s) {
    if (s.fixedNow != 0) return s.fixedNow;
    return static_cast<int64_t>(std::time(nullptr));
}

struct Paths { fs::path user, machine; };

Paths currentPaths(const State& s) {
    if (s.overridePaths) return {s.userOverride, s.machineOverride};
    Paths p;
    try { p.user = defaultUserFile(); } catch (...) {}
    try { p.machine = defaultMachineFile(); } catch (...) {}
    return p;
}

enum class FileRead { Missing, Ok, Unreadable, TooLarge };

FileRead readSmallFile(const fs::path& p, std::string& out) {
    out.clear();
    if (p.empty()) return FileRead::Missing;
    std::error_code ec;
    const fs::file_status st = fs::status(p, ec);
    if (ec || !fs::exists(st)) return FileRead::Missing;
    if (!fs::is_regular_file(st)) return FileRead::Unreadable;
    const uintmax_t sz = fs::file_size(p, ec);
    if (ec) return FileRead::Unreadable;
    if (sz > kMaxKeyFileBytes) return FileRead::TooLarge;
#if defined(_WIN32)
    // Share everything so a concurrent saveKey()/removeKey() (MoveFileEx/DeleteFile) never fails
    // because a render thread happens to be reading the file.
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? FileRead::Missing : FileRead::Unreadable;
    }
    char buf[4096];
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        out.append(buf, got);
        if (out.size() > kMaxKeyFileBytes) break;
    }
    CloseHandle(h);
    if (!ok) { out.clear(); return FileRead::Unreadable; }
#else
    std::ifstream f(p, std::ios::binary);
    if (!f) return FileRead::Unreadable;
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) return FileRead::Unreadable;
    out = ss.str();
#endif
    if (out.size() > kMaxKeyFileBytes) { out.clear(); return FileRead::TooLarge; }
    return FileRead::Ok;
}

// Windows refuses to replace/delete a file another process opened without FILE_SHARE_DELETE
// (virus scanners, backup tools, other hosts): retry briefly before giving up.
template <class Op>
void retryFs(Op op, std::error_code& ec) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        ec.clear();
        op(ec);
        if (!ec) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(attempt < 5 ? 2 : 20));
    }
}

// Drops comment lines ('#' as first non-blank character).
std::string stripComments(const std::string& text) {
    std::string out;
    size_t b = 0;
    while (b < text.size()) {
        size_t e = text.find('\n', b);
        if (e == std::string::npos) e = text.size();
        size_t k = b;
        while (k < e && (text[k] == ' ' || text[k] == '\t' || text[k] == '\r')) ++k;
        if (!(k < e && text[k] == '#')) { out.append(text, b, e - b); out.push_back('\n'); }
        b = e + 1;
    }
    return out;
}

// Value of an (ASCII-named) environment variable, truncated just past the key-length limit so an
// absurd value is rejected by verifyKey() instead of being copied around.
std::string envVar(const char* name) {
#if defined(_WIN32)
    char buf[fmt::kMaxKeyTextChars + 2];
    const DWORD n = GetEnvironmentVariableA(name, buf, DWORD(sizeof(buf)));
    if (n == 0) return std::string();
    if (n >= sizeof(buf)) return std::string(fmt::kMaxKeyTextChars + 1, ' ');
    return std::string(buf, n);
#else
    const char* e = std::getenv(name);
    if (!e) return std::string();
    const size_t n = std::char_traits<char>::length(e);
    return std::string(e, n > fmt::kMaxKeyTextChars ? fmt::kMaxKeyTextChars + 1 : n);
#endif
}

struct Snapshot {
    FileRead    user = FileRead::Missing, machine = FileRead::Missing;
    std::string userText, machineText, envText;
    fs::path    userPath, machinePath;
};

Snapshot loadSnapshot(const Paths& p) {
    Snapshot s;
    s.userPath = p.user;
    s.machinePath = p.machine;
    try { s.user = readSmallFile(p.user, s.userText); } catch (...) { s.user = FileRead::Unreadable; }
    try { s.machine = readSmallFile(p.machine, s.machineText); } catch (...) { s.machine = FileRead::Unreadable; }
    s.envText = envVar(detail::kLicenseEnvVar);
    return s;
}

LicenseInfo trialInfo() {
    LicenseInfo li;
    li.valid = false;
    li.edition = Edition::Trial;
    li.message = "Trial - all features except LUT export, renders carry a watermark. Enter a license key to activate.";
    return li;
}

LicenseInfo evaluateFile(FileRead r, const std::string& text, const fs::path& path, int64_t now, bool& present) {
    present = r != FileRead::Missing;
    LicenseInfo li;
    switch (r) {
        case FileRead::Missing: return trialInfo();
        case FileRead::Unreadable:
            li = trialInfo();
            li.message = "The license file " + pathToUtf8(path) + " cannot be read (check its permissions).";
            return li;
        case FileRead::TooLarge:
            li = trialInfo();
            li.message = "The license file " + pathToUtf8(path) + " is not a license key file.";
            return li;
        case FileRead::Ok: break;
    }
    return verifyKey(stripComments(text), now);
}

LicenseInfo evaluate(const Snapshot& s, int64_t now) {
    bool userPresent = false, machinePresent = false;
    LicenseInfo user = evaluateFile(s.user, s.userText, s.userPath, now, userPresent);
    const bool envPresent = !s.envText.empty();
    LicenseInfo env;
    if (envPresent) env = verifyKey(s.envText, now);
    LicenseInfo machine = evaluateFile(s.machine, s.machineText, s.machinePath, now, machinePresent);

    auto fromEnv = [&env]() { env.message += " (from " + std::string(detail::kLicenseEnvVar) + ")"; return env; };
    auto fromMachine = [&machine]() { machine.message += " (machine-wide license)"; return machine; };
    // 1. paid editions first (user, env, machine), 2. then a valid Trial-edition key
    for (int pass = 0; pass < 2; ++pass) {
        auto ok = [pass](const LicenseInfo& li) { return li.valid && (pass == 1 || li.edition != Edition::Trial); };
        if (userPresent && ok(user)) return user;
        if (envPresent && ok(env)) return fromEnv();
        if (machinePresent && ok(machine)) return fromMachine();
    }

    if (userPresent)    { user.message = "Saved license key: " + user.message; return user; }
    if (envPresent)     { env.message = std::string(detail::kLicenseEnvVar) + ": " + env.message; return env; }
    if (machinePresent) { machine.message = "Machine-wide license key: " + machine.message; return machine; }
    return trialInfo();
}

// Marks the cached status due (keeps it: render threads keep getting the previous status, never
// wait on the mutex while the files are re-read) and discards refreshes already in flight.
void invalidateLocked(State& s) {
    s.lastCheck = std::chrono::steady_clock::time_point{};
    s.lastVerifyNow = 0;
    ++s.generation;
}

// Synchronous reload outside the mutex (saveKey/removeKey on the UI thread).
void reloadNow(State& s) {
    uint64_t gen;
    Paths paths;
    int64_t now;
    {
        std::lock_guard<std::mutex> lk(s.m);
        gen = s.generation;
        paths = currentPaths(s);
        now = clockNow(s);
    }
    LicenseInfo info = evaluate(loadSnapshot(paths), now);
    std::lock_guard<std::mutex> lk(s.m);
    if (gen != s.generation) return;          // invalidated again meanwhile: the next current() reloads
    s.info = info;
    s.have = true;
    s.lastCheck = std::chrono::steady_clock::now();
    s.lastVerifyNow = now;
}

bool setErr(std::string* err, const std::string& msg) {
    if (err) { try { *err = msg; } catch (...) {} }
    return false;
}

} // namespace

// ---- public API -------------------------------------------------------------------------------
LicenseInfo current() {
    try {
        State& s = state();
        std::unique_lock<std::mutex> lk(s.m);
        const auto t = std::chrono::steady_clock::now();
        const int64_t now = clockNow(s);
        const bool due = !s.have ||
                         t - s.lastCheck >= std::chrono::milliseconds(s.recheckMs) ||
                         now - s.lastVerifyNow >= kReverifySeconds || now < s.lastVerifyNow;
        if (!due) return s.info;
        if (s.have && s.refreshing) return s.info;   // another thread is already refreshing

        if (s.have) {
            // Refresh without holding the lock (file I/O + signature check).
            s.refreshing = true;
            const uint64_t gen = s.generation;
            const Paths paths = currentPaths(s);
            lk.unlock();
            LicenseInfo info;
            bool ok = true;
            try { info = evaluate(loadSnapshot(paths), now); } catch (...) { ok = false; }
            lk.lock();
            s.refreshing = false;
            if (ok && gen == s.generation) {
                s.info = info;
                s.lastCheck = t;
                s.lastVerifyNow = now;
                return s.info;
            }
            if (s.have) return s.info;   // keep the previous status if the refresh failed
            // invalidated meanwhile: fall through and reload synchronously
        }
        s.info = evaluate(loadSnapshot(currentPaths(s)), now);
        s.have = true;
        s.lastCheck = t;
        s.lastVerifyNow = now;
        return s.info;
    } catch (...) {
        LicenseInfo li = trialInfo();
        li.message = "License status unavailable (internal error) - running as trial.";
        return li;
    }
}

bool saveKey(const std::string& key, std::string* err) {
    try {
        State& s = state();
        int64_t now;
        Paths paths;
        {
            std::lock_guard<std::mutex> lk(s.m);
            now = clockNow(s);
            paths = currentPaths(s);
        }
        const LicenseInfo li = verifyKey(key, now);
        if (!li.valid) return setErr(err, li.message);
        const std::string canon = detail::canonicalKey(key);
        if (canon.empty()) return setErr(err, "The license key is malformed.");
        if (paths.user.empty()) return setErr(err, "Cannot determine the user settings folder to store the license.");

        std::error_code ec;
        const fs::path dir = paths.user.parent_path();
        fs::create_directories(dir, ec);
        if (ec) return setErr(err, "Cannot create the folder " + pathToUtf8(dir) + ": " + ec.message());

        // Write to a unique temporary file in the same folder, then atomically replace.
        static std::atomic<unsigned> counter{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        const size_t tid = std::hash<std::thread::id>()(std::this_thread::get_id());
        fs::path tmp = paths.user;
        tmp += ".tmp-" + std::to_string(static_cast<unsigned long long>(tick)) + "-" +
               std::to_string(tid % 100000) + "-" + std::to_string(counter.fetch_add(1));
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return setErr(err, "Cannot write to " + pathToUtf8(dir) + " (check folder permissions).");
            f << "# Celluloid Pro license key (" << li.licensee << ", " << fmt::editionName(li.edition) << ")\n"
              << canon << "\n";
            f.flush();
            if (!f) {
                f.close();
                fs::remove(tmp, ec);
                return setErr(err, "Writing the license file failed (disk full?).");
            }
        }
#if !defined(_WIN32)
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        ec.clear();
#endif
        retryFs([&](std::error_code& e) { fs::rename(tmp, paths.user, e); }, ec);
        if (ec) {
            std::error_code ec2;
            fs::remove(tmp, ec2);
            return setErr(err, "Cannot replace " + pathToUtf8(paths.user) + ": " + ec.message());
        }
        {
            std::lock_guard<std::mutex> lk(s.m);
            invalidateLocked(s);
        }
        reloadNow(s);
        return true;
    } catch (...) {
        return setErr(err, "Saving the license failed (internal error).");
    }
}

bool removeKey(std::string* err) {
    try {
        State& s = state();
        Paths paths;
        int64_t now;
        {
            std::lock_guard<std::mutex> lk(s.m);
            paths = currentPaths(s);
            now = clockNow(s);
        }
        if (!paths.user.empty()) {
            std::error_code ec;
            const bool existed = fs::exists(fs::symlink_status(paths.user, ec));
            if (existed) {
                retryFs([&](std::error_code& e) {
                    std::error_code e2;
                    if (!fs::remove(paths.user, e) && !e && fs::exists(fs::symlink_status(paths.user, e2)))
                        e = std::make_error_code(std::errc::device_or_resource_busy);
                }, ec);
                if (ec) {
                    std::lock_guard<std::mutex> lk(s.m);
                    invalidateLocked(s);
                    return setErr(err, "Cannot remove " + pathToUtf8(paths.user) + ": " + ec.message());
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(s.m);
            invalidateLocked(s);
        }
        reloadNow(s);
        // Informational note (return value stays true): another source may still license this machine.
        Snapshot snap = loadSnapshot(paths);
        snap.user = FileRead::Missing;
        const LicenseInfo rest = evaluate(snap, now);
        if (rest.valid && err)
            *err = "The per-user key was removed, but another license still applies: " + rest.message;
        return true;
    } catch (...) {
        return setErr(err, "Removing the license failed (internal error).");
    }
}

namespace detail {

std::string userKeyFilePath() {
    try {
        State& s = state();
        std::lock_guard<std::mutex> lk(s.m);
        return pathToUtf8(currentPaths(s).user);
    } catch (...) { return std::string(); }
}

std::string machineKeyFilePath() {
    try {
        State& s = state();
        std::lock_guard<std::mutex> lk(s.m);
        return pathToUtf8(currentPaths(s).machine);
    } catch (...) { return std::string(); }
}

void invalidateCache() {
    try {
        State& s = state();
        std::lock_guard<std::mutex> lk(s.m);
        invalidateLocked(s);
    } catch (...) {}
}

void setKeyPathsForTesting(const std::string& userFile, const std::string& machineFile) {
    State& s = state();
    std::lock_guard<std::mutex> lk(s.m);
    s.overridePaths = !(userFile.empty() && machineFile.empty());
    s.userOverride = userFile.empty() ? fs::path() : pathFromUtf8(userFile);
    s.machineOverride = machineFile.empty() ? fs::path() : pathFromUtf8(machineFile);
    invalidateLocked(s);
}

void setRecheckIntervalForTesting(int ms) {
    State& s = state();
    std::lock_guard<std::mutex> lk(s.m);
    s.recheckMs = ms < 0 ? 0 : ms;
}

void setClockForTesting(int64_t fixedNowUnix) {
    State& s = state();
    std::lock_guard<std::mutex> lk(s.m);
    s.fixedNow = fixedNowUnix;   // deliberately no invalidation: lets tests exercise the 60 s re-verify
}

} // namespace detail

}} // namespace cel::lic
