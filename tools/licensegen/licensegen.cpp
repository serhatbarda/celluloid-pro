// licensegen - Celluloid Pro license key tool (keypairs, signing, verification, PublicKey.h).
// See tools/licensegen/README.md.  Exit codes: 0 = OK / key valid, 1 = key invalid, 2 = usage or I/O error.
#include "../../src/license/KeyFormat.h"
#include "../../src/license/KeySigner.h"
#include "../../src/license/License.h"
#include "../../src/license/LicenseInternal.h"
#include "../../src/license/PublicKey.h"
#include "../../src/license/DevPublicKey.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace cel::lic;

namespace {

const char* kUsage =
"licensegen - Celluloid Pro license key tool\n"
"\n"
"  licensegen keygen [--out-dir DIR] [--name NAME] [--force]\n"
"      New Ed25519 signing keypair: NAME.private.key (secret!) + NAME.public.key.\n"
"      Default DIR: ~/.celluloid-license-keys. Refuses to write inside a git work tree.\n"
"  licensegen keygen --dev --out-dir DIR [--name NAME] [--force]\n"
"      Development keypair (clearly marked; accepted only by -DCEL_DEV_LICENSE=ON builds).\n"
"  licensegen header --pub FILE [--pub FILE ...] --out PublicKey.h [--provisional]\n"
"  licensegen header --dev --pub FILE --out DevPublicKey.h\n"
"      Generate the compiled-in public key header (several --pub = key rotation).\n"
"  licensegen pubkey --key PRIVATE.key            print the public key file for a private key\n"
"  licensegen fingerprint --pub FILE              print the key fingerprint\n"
"  licensegen sign --key PRIVATE.key --licensee TEXT [--edition indie|pro|studio|trial]\n"
"                  [--id ID] [--issued now|YYYY-MM-DD|@UNIX] [--expires never|YYYY-MM-DD|+Nd|@UNIX]\n"
"                  [--seats N] [--product celluloid-pro] [--json]\n"
"      Issue a key. --expires YYYY-MM-DD = valid through the end of that day (UTC).\n"
"  licensegen verify [--pub FILE ...] [--now YYYY-MM-DD|@UNIX] [--json] KEY|-\n"
"      Verify a key ('-' reads it from stdin). Without --pub: the compiled-in production\n"
"      key(s) and the development key.\n";

int usage(const char* msg = nullptr) {
    if (msg) std::cerr << "licensegen: " << msg << "\n\n";
    std::cerr << kUsage;
    return 2;
}

int fail(const std::string& msg) {
    std::cerr << "licensegen: " << msg << "\n";
    return 2;
}

struct Args {
    std::vector<std::string> pos;
    std::vector<std::pair<std::string, std::string>> opt;
    std::vector<std::string> flags;
    bool has(const std::string& f) const {
        for (auto& x : flags) if (x == f) return true;
        return false;
    }
    bool get(const std::string& k, std::string& v) const {
        for (auto& p : opt) if (p.first == k) { v = p.second; return true; }
        return false;
    }
    std::vector<std::string> all(const std::string& k) const {
        std::vector<std::string> r;
        for (auto& p : opt) if (p.first == k) r.push_back(p.second);
        return r;
    }
};

bool parseArgs(int argc, char** argv, int start, Args& a, std::string& err) {
    static const char* valueOpts[] = {"--out-dir", "--name", "--pub", "--out", "--key", "--licensee",
                                      "--edition", "--id", "--issued", "--expires", "--seats",
                                      "--product", "--now"};
    static const char* flagOpts[] = {"--force", "--dev", "--provisional", "--json"};
    for (int i = start; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-" || s.rfind("--", 0) != 0) { a.pos.push_back(s); continue; }
        std::string key = s, val;
        const size_t eq = s.find('=');
        bool hasVal = false;
        if (eq != std::string::npos) { key = s.substr(0, eq); val = s.substr(eq + 1); hasVal = true; }
        bool known = false;
        for (const char* v : valueOpts) if (key == v) {
            known = true;
            if (!hasVal) {
                if (i + 1 >= argc) { err = "missing value for " + key; return false; }
                val = argv[++i];
            }
            a.opt.emplace_back(key, val);
        }
        for (const char* f : flagOpts) if (key == f) {
            known = true;
            if (hasVal) { err = key + " takes no value"; return false; }
            a.flags.push_back(key);
        }
        if (!known) { err = "unknown option " + key; return false; }
    }
    return true;
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !f.bad();
}

fs::path homeDir() {
#if defined(_WIN32)
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, 1024);
    return (n > 0 && n < 1024) ? fs::path(std::wstring(buf, n)) : fs::path();
#else
    const char* h = std::getenv("HOME");
    return h ? fs::path(h) : fs::path();
#endif
}

// Returns the work-tree root if `dir` (or a parent) contains a .git entry.
bool insideGitWorkTree(const fs::path& dir, fs::path& root) {
    std::error_code ec;
    fs::path p = fs::absolute(dir, ec);
    if (ec) return false;
    p = p.lexically_normal();
    while (true) {
        if (fs::exists(p / ".git", ec)) { root = p; return true; }
        const fs::path parent = p.parent_path();
        if (parent.empty() || parent == p) return false;
        p = parent;
    }
}

// Creates a file that must not exist yet (unless force), owner-only permissions on POSIX.
bool writeNewFile(const fs::path& p, const std::string& content, bool secret, bool force, std::string& err) {
    std::error_code ec;
    if (fs::exists(p, ec) && !force) { err = p.string() + " already exists (use --force to overwrite)"; return false; }
#if defined(_WIN32)
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot create " + p.string(); return false; }
    f << content;
    f.close();
    if (!f) { err = "write failed: " + p.string(); return false; }
    (void)secret;
#else
    if (force) ::unlink(p.c_str());
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, secret ? 0600 : 0644);
    if (fd < 0) { err = "cannot create " + p.string() + ": " + std::strerror(errno); return false; }
    size_t off = 0;
    while (off < content.size()) {
        const ssize_t w = ::write(fd, content.data() + off, content.size() - off);
        if (w < 0) { if (errno == EINTR) continue; ::close(fd); err = "write failed: " + p.string(); return false; }
        off += size_t(w);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) { err = "write failed: " + p.string(); return false; }
#endif
    return true;
}

bool loadPrivateKey(const std::string& path, uint8_t seed[32], std::string& err) {
    std::string text;
    if (!readFile(path, text)) { err = "cannot read " + path; return false; }
#if !defined(_WIN32)
    struct stat st;
    const bool devKey = text.find("DEVELOPMENT") != std::string::npos;   // public on purpose
    if (!devKey && ::stat(path.c_str(), &st) == 0 && (st.st_mode & 077) != 0)
        std::cerr << "licensegen: warning: " << path << " is readable by other users (chmod 600 it)\n";
#endif
    const bool ok = fmt::parsePrivateKeyFile(text, seed);
    fmt::secureWipe(&text[0], text.size());
    if (!ok) { err = path + " is not a licensegen private key file (CELPRIV1:...)"; return false; }
    return true;
}

bool loadPublicKey(const std::string& path, uint8_t pub[32], std::string& err) {
    std::string text;
    if (!readFile(path, text)) { err = "cannot read " + path; return false; }
    if (!fmt::parsePublicKeyFile(text, pub)) { err = path + " is not a licensegen public key file (CELPUB1:...)"; return false; }
    return true;
}

bool parseInt64(const std::string& s, int64_t& v) {
    if (s.empty() || s.size() > 18) return false;
    int64_t r = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; r = r * 10 + (c - '0'); }
    v = r;
    return true;
}

// now | YYYY-MM-DD | @UNIX
bool parseWhen(const std::string& s, int64_t now, int64_t& out) {
    if (s == "now") { out = now; return true; }
    if (!s.empty() && s[0] == '@') return parseInt64(s.substr(1), out);
    return fmt::parseDateUtc(s, out);
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o.push_back(char(c));
        }
    }
    return o;
}

std::string headerText(const std::vector<std::vector<uint8_t>>& keys, bool dev, bool provisional) {
    std::ostringstream h;
    h << "// GENERATED by tools/licensegen (`licensegen header" << (dev ? " --dev" : "")
      << (provisional ? " --provisional" : "") << "`). Do not edit by hand.\n";
    if (dev) {
        h << "// DEVELOPMENT public key. Compiled in ONLY when the build is configured with\n"
             "// -DCEL_DEV_LICENSE=ON; its private half is tools/licensegen/dev-keys/ (committed to the repo,\n"
             "// so keys signed with it prove nothing). Release builds never contain this key.\n";
    } else {
        h << "// PRODUCTION public key(s): license keys signed by the matching private key(s) are accepted.\n"
             "// Several keys = key rotation (keep the old one until all customers have new keys).\n";
        if (provisional)
            h << "// PROVISIONAL: generated on the development machine during the build phase. Before the first\n"
                 "// commercial release, run `licensegen keygen` on the product owner's own offline machine and\n"
                 "// regenerate this header (see tools/licensegen/README.md).\n";
    }
    h << "#pragma once\n#include <cstdint>\n\nnamespace cel { namespace lic { namespace keys {\n\n";
    auto emit = [&](const std::vector<uint8_t>& k) {
        h << "    {";
        for (size_t i = 0; i < k.size(); ++i) {
            if (i % 8 == 0) h << "\n        ";
            char b[8]; std::snprintf(b, sizeof(b), "0x%02x,", k[i]); h << b;
            if (i % 8 != 7) h << " ";
        }
        h << "\n    }";
    };
    if (dev) {
        h << "// fingerprint " << fmt::keyFingerprint(keys[0].data()) << "\n";
        h << "static const uint8_t kDevKey[32] =\n";
        emit(keys[0]);
        h << ";\n";
    } else {
        h << "constexpr int kProductionKeyCount = " << keys.size() << ";\n";
        h << "constexpr bool kProductionKeyProvisional = " << (provisional ? "true" : "false") << ";\n";
        h << "// fingerprints:";
        for (auto& k : keys) h << " " << fmt::keyFingerprint(k.data());
        h << "\nstatic const uint8_t kProductionKeys[kProductionKeyCount][32] = {\n";
        for (size_t i = 0; i < keys.size(); ++i) { emit(keys[i]); h << (i + 1 < keys.size() ? ",\n" : "\n"); }
        h << "};\n";
    }
    h << "\n}}} // namespace cel::lic::keys\n";
    return h.str();
}

// ---- commands ------------------------------------------------------------------------------
int cmdKeygen(const Args& a) {
    const bool dev = a.has("--dev");
    std::string outDir, name;
    if (!a.get("--name", name)) name = dev ? "celluloid-dev" : "celluloid-signing";
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
            return fail("--name may only contain [A-Za-z0-9._-]");
    fs::path dir;
    if (a.get("--out-dir", outDir)) dir = fs::path(outDir);
    else if (dev) return fail("--dev requires an explicit --out-dir (tools/licensegen/dev-keys)");
    else {
        const fs::path h = homeDir();
        if (h.empty()) return fail("cannot determine the home folder; pass --out-dir");
        dir = h / ".celluloid-license-keys";
    }
    fs::path repoRoot;
    if (!dev && insideGitWorkTree(dir, repoRoot))
        return fail("refusing to write a PRODUCTION private key inside the git work tree " + repoRoot.string() +
                    ". Choose a folder outside every repository (default: ~/.celluloid-license-keys).");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return fail("cannot create " + dir.string() + ": " + ec.message());
#if !defined(_WIN32)
    if (!dev) fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    uint8_t seed[32], pub[32];
    if (!sign::osRandomBytes(seed, sizeof(seed))) return fail("the operating system RNG is unavailable");
    sign::derivePublicKey(seed, pub);
    const std::string label = dev
        ? "Celluloid Pro DEVELOPMENT signing key - NOT FOR PRODUCTION. Committed to the repository on purpose."
        : "Celluloid Pro PRODUCTION license signing key";
    std::string privText = fmt::privateKeyFileText(seed, label);
    fmt::secureWipe(seed, sizeof(seed));
    std::string err;
    const fs::path privPath = dir / (name + ".private.key");
    const fs::path pubPath = dir / (name + ".public.key");
    const bool okPriv = writeNewFile(privPath, privText, true, a.has("--force"), err);
    fmt::secureWipe(&privText[0], privText.size());
    if (!okPriv) return fail(err);
    if (!writeNewFile(pubPath, fmt::publicKeyFileText(pub, dev ? "Celluloid Pro DEVELOPMENT public key"
                                                               : "Celluloid Pro production public key"),
                      false, a.has("--force"), err))
        return fail(err);
    std::cout << "private key: " << privPath.string() << (dev ? "  (DEVELOPMENT)" : "  (KEEP SECRET, BACK IT UP)") << "\n"
              << "public key:  " << pubPath.string() << "\n"
              << "fingerprint: " << fmt::keyFingerprint(pub) << "\n"
              << "next: licensegen header " << (dev ? "--dev " : "") << "--pub " << pubPath.string()
              << " --out src/license/" << (dev ? "DevPublicKey.h" : "PublicKey.h") << "\n";
    return 0;
}

int cmdHeader(const Args& a) {
    const bool dev = a.has("--dev");
    std::string out;
    if (!a.get("--out", out)) return usage("header: --out is required");
    const std::vector<std::string> pubs = a.all("--pub");
    if (pubs.empty()) return usage("header: at least one --pub is required");
    if (dev && pubs.size() != 1) return usage("header --dev takes exactly one --pub");
    if (pubs.size() > 8) return usage("header: at most 8 production keys");
    std::vector<std::vector<uint8_t>> keys;
    for (auto& p : pubs) {
        std::vector<uint8_t> k(32);
        std::string err;
        if (!loadPublicKey(p, k.data(), err)) return fail(err);
        keys.push_back(k);
    }
    const std::string text = headerText(keys, dev, a.has("--provisional"));
    std::ofstream f(fs::path(out), std::ios::binary | std::ios::trunc);
    if (!f || !(f << text) || (f.close(), !f)) return fail("cannot write " + out);
    std::cout << "wrote " << out << "\n";
    return 0;
}

int cmdPubkey(const Args& a) {
    std::string keyPath, err;
    if (!a.get("--key", keyPath)) return usage("pubkey: --key is required");
    uint8_t seed[32], pub[32];
    if (!loadPrivateKey(keyPath, seed, err)) return fail(err);
    sign::derivePublicKey(seed, pub);
    fmt::secureWipe(seed, sizeof(seed));
    std::cout << fmt::publicKeyFileText(pub, "Celluloid Pro public key (derived from " + keyPath + ")");
    return 0;
}

int cmdFingerprint(const Args& a) {
    std::string p, err;
    if (!a.get("--pub", p)) return usage("fingerprint: --pub is required");
    uint8_t pub[32];
    if (!loadPublicKey(p, pub, err)) return fail(err);
    std::cout << fmt::keyFingerprint(pub) << "\n";
    return 0;
}

int cmdSign(const Args& a) {
    std::string keyPath, err, v;
    if (!a.get("--key", keyPath)) return usage("sign: --key is required");
    fmt::Payload p;
    if (!a.get("--licensee", p.licensee)) return usage("sign: --licensee is required");
    // tidy surrounding blanks from web-form input; everything else is validated strictly
    while (!p.licensee.empty() && std::isspace(static_cast<unsigned char>(p.licensee.back()))) p.licensee.pop_back();
    while (!p.licensee.empty() && std::isspace(static_cast<unsigned char>(p.licensee.front()))) p.licensee.erase(0, 1);
    if (a.get("--edition", v) && !fmt::parseEdition(v, p.edition)) return usage("sign: --edition must be trial|indie|pro|studio");
    if (a.get("--product", v)) p.product = v;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    p.issued = now;
    if (a.get("--issued", v) && !parseWhen(v, now, p.issued)) return usage("sign: bad --issued (now|YYYY-MM-DD|@UNIX)");
    p.expires = 0;
    if (a.get("--expires", v) && v != "never" && v != "0") {
        if (!v.empty() && v[0] == '+') {
            int64_t days = 0;
            if (v.size() < 3 || (v.back() != 'd' && v.back() != 'D') || !parseInt64(v.substr(1, v.size() - 2), days) || days < 1)
                return usage("sign: bad --expires +Nd");
            p.expires = p.issued + days * 86400;
        } else if (!v.empty() && v[0] == '@') {
            if (!parseInt64(v.substr(1), p.expires)) return usage("sign: bad --expires @UNIX");
        } else {
            int64_t day;
            if (!fmt::parseDateUtc(v, day)) return usage("sign: bad --expires (never|YYYY-MM-DD|+Nd|@UNIX)");
            p.expires = day + 86400;   // valid through the end of that day (UTC)
        }
    }
    if (a.get("--seats", v)) {
        int64_t s;
        if (!parseInt64(v, s) || s < 1 || s > 65535) return usage("sign: --seats must be 1..65535");
        p.seats = uint32_t(s);
    }
    if (!a.get("--id", p.id) && !sign::randomLicenseId(p.id)) return fail("the operating system RNG is unavailable");
    if (p.product != fmt::kProduct)
        std::cerr << "licensegen: warning: product '" << p.product << "' will be rejected by Celluloid Pro\n";
    if (p.edition == Edition::Trial)
        std::cerr << "licensegen: note: Trial edition keys keep the watermark on\n";

    uint8_t seed[32];
    if (!loadPrivateKey(keyPath, seed, err)) return fail(err);
    std::string key;
    const bool ok = sign::signLicenseKey(p, seed, key, &err);
    fmt::secureWipe(seed, sizeof(seed));
    if (!ok) return fail("sign: " + err);

    if (a.has("--json")) {
        std::cout << "{\"key\":\"" << key << "\",\"product\":\"" << jsonEscape(p.product)
                  << "\",\"edition\":\"" << fmt::editionName(p.edition) << "\",\"licensee\":\"" << jsonEscape(p.licensee)
                  << "\",\"id\":\"" << jsonEscape(p.id) << "\",\"issued\":" << p.issued
                  << ",\"expires\":" << p.expires << ",\"seats\":" << p.seats << "}\n";
    } else {
        std::cout << key << "\n";
    }
    return 0;
}

int cmdVerify(const Args& a) {
    if (a.pos.size() != 1) return usage("verify: exactly one KEY (or '-') expected");
    std::string key = a.pos[0];
    if (key == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        key = ss.str();
    }
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::string v;
    if (a.get("--now", v) && !parseWhen(v, now, now)) return usage("verify: bad --now");

    std::vector<std::vector<uint8_t>> store;
    std::vector<detail::TrustedKey> trusted;
    const std::vector<std::string> pubs = a.all("--pub");
    if (!pubs.empty()) {
        for (auto& p : pubs) {
            std::vector<uint8_t> k(32);
            std::string err;
            if (!loadPublicKey(p, k.data(), err)) return fail(err);
            store.push_back(k);
        }
        for (auto& k : store) trusted.push_back({k.data(), false});
    } else {
        for (int i = 0; i < keys::kProductionKeyCount; ++i) trusted.push_back({keys::kProductionKeys[i], false});
        trusted.push_back({keys::kDevKey, true});
    }
    const LicenseInfo li = detail::verifyKeyWithKeys(key, now, trusted.data(), trusted.size());
    const bool signedOk = !li.licenseId.empty();
    if (a.has("--json")) {
        std::cout << "{\"valid\":" << (li.valid ? "true" : "false") << ",\"message\":\"" << jsonEscape(li.message) << "\"";
        if (signedOk)
            std::cout << ",\"edition\":\"" << fmt::editionName(li.edition) << "\",\"licensee\":\"" << jsonEscape(li.licensee)
                      << "\",\"id\":\"" << jsonEscape(li.licenseId) << "\",\"issued\":" << li.issuedUnix
                      << ",\"expires\":" << li.expiresUnix << ",\"seats\":" << li.seats;
        std::cout << "}\n";
    } else {
        std::cout << "valid:    " << (li.valid ? "yes" : "NO") << "\n"
                  << "status:   " << li.message << "\n";
        if (signedOk) {
            std::cout << "licensee: " << li.licensee << "\n"
                      << "id:       " << li.licenseId << "\n"
                      << "issued:   " << fmt::formatDateUtc(li.issuedUnix) << " (@" << li.issuedUnix << ")\n"
                      << "expires:  " << (li.expiresUnix ? fmt::formatDateUtc(li.expiresUnix - 1) + " end of day UTC (@" +
                                                           std::to_string(li.expiresUnix) + ")" : std::string("never")) << "\n"
                      << "seats:    " << li.seats << "\n";
        }
    }
    return li.valid ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) return usage();
        const std::string cmd = argv[1];
        if (cmd == "help" || cmd == "--help" || cmd == "-h") { std::cout << kUsage; return 0; }
        Args a;
        std::string err;
        if (!parseArgs(argc, argv, 2, a, err)) return usage(err.c_str());
        if (cmd != "verify" && !a.pos.empty()) return usage(("unexpected argument " + a.pos[0]).c_str());
        if (cmd == "keygen") return cmdKeygen(a);
        if (cmd == "header") return cmdHeader(a);
        if (cmd == "pubkey") return cmdPubkey(a);
        if (cmd == "fingerprint") return cmdFingerprint(a);
        if (cmd == "sign") return cmdSign(a);
        if (cmd == "verify") return cmdVerify(a);
        return usage(("unknown command " + cmd).c_str());
    } catch (const std::exception& e) {
        return fail(std::string("error: ") + e.what());
    }
}
