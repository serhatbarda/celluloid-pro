// tests/license - persistence (saveKey/removeKey/current) in a temporary HOME.
// Needs a build that accepts the development key (CEL_DEV_LICENSE=ON); skipped otherwise.
#include "TestUtil.h"
#include "../../src/license/License.h"
#include "../../src/license/KeyFormat.h"
#include "../../src/license/KeySigner.h"
#include "../../src/license/LicenseInternal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
  #include <sys/stat.h>
  #include <unistd.h>
#endif

using namespace cel::lic;
namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

bool devSeed(uint8_t seed[32]) {
    return fmt::parsePrivateKeyFile(
        readText(std::string(CEL_LICENSE_REPO_ROOT) + "/tools/licensegen/dev-keys/celluloid-dev.private.key"), seed);
}

std::string devKey(const std::string& licensee, int64_t issued, int64_t expires = 0, Edition ed = Edition::Pro) {
    uint8_t seed[32];
    if (!devSeed(seed)) return std::string();
    fmt::Payload p;
    p.edition = ed;
    p.licensee = licensee;
    p.id = "CP-PERSIST";
    p.issued = issued;
    p.expires = expires;
    std::string key, err;
    sign::signLicenseKey(p, seed, key, &err);
    return key;
}

struct TempDir {
    fs::path path;
    TempDir() {
        const char* base = std::getenv("TMPDIR");
        std::string tmpl = std::string(base && *base ? base : "/tmp") + "/cel-license-test-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
#if defined(_WIN32)
        path = fs::temp_directory_path() / ("cel-license-test-" + std::to_string(std::rand()));
        fs::create_directories(path);
#else
        if (mkdtemp(buf.data())) path = buf.data();
#endif
    }
    ~TempDir() { std::error_code ec; if (!path.empty()) fs::remove_all(path, ec); }
};

bool skipIfNoDev() {
    if (detail::devKeysAccepted()) return false;
    std::printf("    SKIPPED (needs CEL_DEV_LICENSE=ON)\n");
    return true;
}

const int64_t kT0 = 1791504000;   // 2026-10-09

} // namespace

CEL_TEST(persist_default_paths_follow_home) {
#if defined(__linux__)
    TempDir home;
    const char* oldHome = std::getenv("HOME");
    const std::string saved = oldHome ? oldHome : "";
    const char* oldXdg = std::getenv("XDG_CONFIG_HOME");
    const std::string savedXdg = oldXdg ? oldXdg : "";
    setenv("HOME", home.path.c_str(), 1);
    unsetenv("XDG_CONFIG_HOME");
    detail::setKeyPathsForTesting("", "");
    CHECK(detail::userKeyFilePath() == (home.path / ".config/reafilm/license.key").string());
    CHECK(detail::machineKeyFilePath() == "/etc/reafilm/license.key");
    setenv("XDG_CONFIG_HOME", (home.path / "xdg").c_str(), 1);
    CHECK(detail::userKeyFilePath() == (home.path / "xdg/reafilm/license.key").string());
    setenv("XDG_CONFIG_HOME", "relative/not/allowed", 1);   // XDG spec: ignore relative paths
    CHECK(detail::userKeyFilePath() == (home.path / ".config/reafilm/license.key").string());
    unsetenv("XDG_CONFIG_HOME");

    if (!skipIfNoDev() && !fs::exists("/etc/reafilm/license.key") && !fs::exists("/etc/celluloid/license.key")) {
        detail::setClockForTesting(kT0);
        CHECK(needsWatermark(current()));
        std::string err;
        CHECK_MSG(saveKey(devKey("Home User", kT0 - 86400), &err), err);
        CHECK(fs::exists(home.path / ".config/reafilm/license.key"));
        const LicenseInfo li = current();
        CHECK(li.valid && li.licensee == "Home User");
        CHECK(removeKey(&err));
        CHECK(!fs::exists(home.path / ".config/reafilm/license.key"));
        CHECK(needsWatermark(current()));

        // a key activated before the rename (~/.config/celluloid) is still read, and removed too
        const fs::path legacy = home.path / ".config/celluloid/license.key";
        fs::create_directories(legacy.parent_path());
        writeText(legacy, devKey("Legacy User", kT0 - 86400) + "\n");
        detail::invalidateCache();
        const LicenseInfo lg = current();
        CHECK(lg.valid && lg.licensee == "Legacy User");
        CHECK(removeKey(&err));
        CHECK(!fs::exists(legacy));
        CHECK(needsWatermark(current()));
        detail::setClockForTesting(0);
    }
    if (saved.empty()) unsetenv("HOME"); else setenv("HOME", saved.c_str(), 1);
    if (!savedXdg.empty()) setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1);
#elif defined(_WIN32)
    detail::setKeyPathsForTesting("", "");
    const std::string u = detail::userKeyFilePath(), m = detail::machineKeyFilePath();
    std::printf("    user: %s\n    machine: %s\n", u.c_str(), m.c_str());
    CHECK(u.size() > 20 && u.compare(u.size() - 20, 20, "\\Reafilm\\license.key") == 0);
    CHECK(m.size() > 20 && m.compare(m.size() - 20, 20, "\\Reafilm\\license.key") == 0 && m != u);
#elif defined(__APPLE__)
    detail::setKeyPathsForTesting("", "");
    const std::string u = detail::userKeyFilePath();
    CHECK(contains(u, "/Library/Application Support/Reafilm/license.key") && u[0] == '/');
    CHECK(detail::machineKeyFilePath() == "/Library/Application Support/Reafilm/license.key");
#else
    CHECK(!detail::userKeyFilePath().empty());
#endif
}

CEL_TEST(persist_save_load_remove_roundtrip) {
    if (skipIfNoDev()) return;
    TempDir tmp;
    const fs::path user = tmp.path / "user dir" / "Reafilm" / "license.key";
    const fs::path machine = tmp.path / "machine" / "license.key";
    detail::setKeyPathsForTesting(user.string(), machine.string());
    detail::setClockForTesting(kT0);
    detail::setRecheckIntervalForTesting(0);

    LicenseInfo li = current();
    CHECK(!li.valid && needsWatermark(li) && contains(li.message, "Trial"));

    std::string err;
    CHECK(!saveKey("CELL1-NOTAKEY", &err) && !err.empty());
    CHECK(!fs::exists(user));
    const std::string expired = devKey("Old Customer", kT0 - 400 * 86400, kT0 - 30 * 86400);
    err.clear();
    CHECK(!saveKey(expired, &err) && contains(err, "expired"));

    // pasted with whitespace + lower case -> stored canonical
    const std::string key = devKey("Jane Colourist", kT0 - 86400);
    std::string messy = "  ";
    for (char c : key) messy.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    messy += "\r\n";
    err.clear();
    CHECK_MSG(saveKey(messy, &err), err);
    CHECK(fs::exists(user));
    const std::string content = readText(user);
    CHECK(contains(content, key + "\n") && contains(content, "# Reafilm Pro license key"));
#if !defined(_WIN32)
    struct stat st;
    CHECK(::stat(user.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
    for (auto& e : fs::directory_iterator(user.parent_path()))   // no temp files left behind
        CHECK_MSG(e.path().filename() == "license.key", e.path().string());
    li = current();
    CHECK(li.valid && li.licensee == "Jane Colourist" && !needsWatermark(li));

    // invalid save leaves the existing key alone
    CHECK(!saveKey("CELL1-AAAA", &err));
    CHECK(current().valid);

    // replacing with another valid key
    CHECK(saveKey(devKey("Second Owner", kT0 - 3600, 0, Edition::Studio), &err));
    li = current();
    CHECK(li.valid && li.licensee == "Second Owner" && li.edition == Edition::Studio);

    // external edits are picked up (recheck interval 0)
    writeText(user, "garbage\n");
    li = current();
    CHECK(!li.valid && contains(li.message, "Saved license key:"));
    writeText(user, "# comment line written by an admin\n" + key + "\n# trailing comment\n");
    CHECK(current().valid);

    err.clear();
    CHECK(removeKey(&err));
    CHECK(err.empty());
    CHECK(!fs::exists(user));
    CHECK(needsWatermark(current()));
    CHECK(removeKey(&err));   // removing twice is fine

    detail::setRecheckIntervalForTesting(1500);
    detail::setClockForTesting(0);
    detail::setKeyPathsForTesting("", "");
}

CEL_TEST(persist_machine_wide_env_and_precedence) {
    if (skipIfNoDev()) return;
    TempDir tmp;
    const fs::path user = tmp.path / "u" / "license.key";
    const fs::path machine = tmp.path / "m" / "license.key";
    detail::setKeyPathsForTesting(user.string(), machine.string());
    detail::setClockForTesting(kT0);
    detail::setRecheckIntervalForTesting(0);

    writeText(machine, "# Studio X - 40 seats, contact IT\n" + devKey("Studio X", kT0 - 86400, 0, Edition::Studio) + "\n");
    LicenseInfo li = current();
    CHECK(li.valid && li.licensee == "Studio X" && contains(li.message, "machine-wide"));

    // a broken per-user file does not hide a valid machine-wide license
    writeText(user, "CELL1-BROKEN\n");
    li = current();
    CHECK(li.valid && li.licensee == "Studio X");

    // a valid per-user key wins
    writeText(user, devKey("Freelancer", kT0 - 86400) + "\n");
    CHECK(current().licensee == "Freelancer");

    std::string err;
    CHECK(removeKey(&err));
    CHECK(contains(err, "another license still applies"));
    CHECK(current().licensee == "Studio X");

    // environment variable (render nodes) sits between user and machine
#if !defined(_WIN32)
    setenv(detail::kLicenseEnvVar, devKey("Render Farm", kT0 - 86400).c_str(), 1);
    li = current();
    CHECK(li.valid && li.licensee == "Render Farm" && contains(li.message, "REAFILM_LICENSE_KEY"));
    setenv(detail::kLicenseEnvVar, "CELL1-INVALID", 1);
    CHECK(current().licensee == "Studio X");
    unsetenv(detail::kLicenseEnvVar);
    // the pre-rename variable still works when the new one is unset
    setenv(detail::kLegacyLicenseEnvVar, devKey("Old Farm", kT0 - 86400).c_str(), 1);
    li = current();
    CHECK(li.valid && li.licensee == "Old Farm" && contains(li.message, "CELLULOID_LICENSE_KEY"));
    unsetenv(detail::kLegacyLicenseEnvVar);
#endif
    // a leftover Trial-edition (evaluation) key in the user file does not shadow a paid key
    writeText(user, devKey("Evaluator", kT0 - 86400, 0, Edition::Trial) + "\n");
    li = current();
    CHECK(li.valid && li.licensee == "Studio X" && li.edition == Edition::Studio && !needsWatermark(li));
#if !defined(_WIN32)
    setenv(detail::kLicenseEnvVar, devKey("Render Farm", kT0 - 86400).c_str(), 1);
    li = current();
    CHECK(li.valid && li.licensee == "Render Farm" && li.edition == Edition::Pro);
    unsetenv(detail::kLicenseEnvVar);
#endif
    fs::remove(machine);
    li = current();                               // no paid key left: the evaluation key applies
    CHECK(li.valid && li.licensee == "Evaluator" && li.edition == Edition::Trial && needsWatermark(li));
    fs::remove(user);
    li = current();
    CHECK(!li.valid && contains(li.message, "Trial"));

    // a file that is too large / a directory instead of a file
    writeText(machine, std::string(100 * 1024, 'A'));
    li = current();
    CHECK(!li.valid && contains(li.message, "not a license key file"));
    fs::remove(machine);
    fs::create_directories(machine);
    li = current();
    CHECK(!li.valid && contains(li.message, "cannot be read"));

    detail::setRecheckIntervalForTesting(1500);
    detail::setClockForTesting(0);
    detail::setKeyPathsForTesting("", "");
}

CEL_TEST(persist_unicode_paths_and_symlinks) {
    if (skipIfNoDev()) return;
    TempDir tmp;
    const fs::path dir = tmp.path / fs::u8path("Lizenz \xc3\x9cnic\xc3\xb6" "de \xe6\x98\xa0\xe7\x94\xbb");
    const fs::path user = dir / "license.key";
    detail::setKeyPathsForTesting(user.u8string(), (tmp.path / "none").u8string());
    detail::setClockForTesting(kT0);
    detail::setRecheckIntervalForTesting(0);
    std::string err;
    CHECK_MSG(saveKey(devKey("Unicode Path", kT0 - 10), &err), err);
    CHECK(fs::exists(user));
    CHECK(current().licensee == "Unicode Path");
    CHECK(detail::userKeyFilePath() == user.u8string());
#if !defined(_WIN32)
    // the key file may be a symlink (e.g. managed by configuration management)
    const fs::path real = tmp.path / "real.key";
    writeText(real, devKey("Linked", kT0 - 10) + "\n");
    fs::remove(user);
    fs::create_symlink(real, user);
    CHECK(current().licensee == "Linked");
    CHECK(removeKey(&err));                       // removes the link, not the target
    CHECK(!fs::exists(fs::symlink_status(user)) && fs::exists(real));
#endif
    detail::setRecheckIntervalForTesting(1500);
    detail::setClockForTesting(0);
    detail::setKeyPathsForTesting("", "");
}

CEL_TEST(persist_cache_expiry_and_recheck_interval) {
    if (skipIfNoDev()) return;
    TempDir tmp;
    const fs::path user = tmp.path / "license.key";
    detail::setKeyPathsForTesting(user.string(), (tmp.path / "none").string());
    detail::setClockForTesting(kT0);
    detail::setRecheckIntervalForTesting(3600 * 1000);   // files effectively never re-read

    const int64_t expires = kT0 + 3600;
    writeText(user, devKey("Monthly", kT0 - 86400, expires) + "\n");
    CHECK(current().valid);
    writeText(user, "garbage\n");                      // not noticed within the interval...
    CHECK(current().valid);
    detail::invalidateCache();                         // ...until invalidated
    CHECK(!current().valid);

    writeText(user, devKey("Monthly", kT0 - 86400, expires) + "\n");
    detail::invalidateCache();
    CHECK(current().valid);
    // the cached status is re-verified at least every 60 s, so the key expires on time
    detail::setClockForTesting(expires + fmt::kClockSkewSeconds + 30);
    LicenseInfo li = current();
    CHECK(!li.valid && contains(li.message, "expired"));

    detail::setRecheckIntervalForTesting(1500);
    detail::setClockForTesting(0);
    detail::setKeyPathsForTesting("", "");
}

CEL_TEST(persist_thread_safety) {
    if (skipIfNoDev()) return;
    TempDir tmp;
    const fs::path user = tmp.path / "license.key";
    detail::setKeyPathsForTesting(user.string(), (tmp.path / "none").string());
    detail::setClockForTesting(kT0);
    detail::setRecheckIntervalForTesting(0);
    const std::string key = devKey("Concurrent", kT0 - 86400);

    std::atomic<bool> stop{false};
    std::atomic<long> calls{0}, inconsistent{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t)
        readers.emplace_back([&]() {
            while (!stop.load()) {
                const LicenseInfo li = current();
                const bool ok = li.valid ? (li.licensee == "Concurrent" && !needsWatermark(li))
                                         : (needsWatermark(li) && !li.message.empty());
                if (!ok) ++inconsistent;
                ++calls;
            }
        });
    std::string err;
    int saves = 0;
    for (int i = 0; i < 150; ++i) {
        if (saveKey(key, &err)) ++saves;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        removeKey(&err);
    }
    CHECK(saveKey(key, &err));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stop = true;
    for (auto& th : readers) th.join();
    std::printf("    %ld concurrent current() calls, %d save/remove cycles\n", calls.load(), saves);
    CHECK(saves == 150);
    CHECK(inconsistent.load() == 0);
    CHECK(current().valid);

    detail::setRecheckIntervalForTesting(1500);
    detail::setClockForTesting(0);
    detail::setKeyPathsForTesting("", "");
}
