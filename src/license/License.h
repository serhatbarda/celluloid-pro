// Reafilm Pro - licensing (offline, Ed25519-signed keys).  FIXED (architect). See SPEC §9.
#pragma once
#include <cstdint>
#include <string>

namespace cel { namespace lic {

enum class Edition : int { Trial = 0, Indie = 1, Pro = 2, Studio = 3 };

struct LicenseInfo {
    bool        valid = false;     // signature + product + dates OK
    Edition     edition = Edition::Trial;
    std::string licensee;          // name or email
    std::string licenseId;
    int64_t     issuedUnix = 0;
    int64_t     expiresUnix = 0;   // 0 = perpetual
    int         seats = 1;
    std::string message;           // human-readable status ("Licensed to … (Pro)", or why invalid)
};

// Verify a license key string (as pasted by the user). Pure, no I/O.
LicenseInfo verifyKey(const std::string& key, int64_t nowUnix);

// Persisted activation: per-user config file
//   macOS:   ~/Library/Application Support/Reafilm/license.key
//   Windows: %APPDATA%\Reafilm\license.key
//   Linux:   $XDG_CONFIG_HOME/reafilm/license.key (or ~/.config/...)
// (also checks a machine-wide location for studio deployments: /Library/Application Support/
//  Reafilm/, %PROGRAMDATA%\Reafilm\, /etc/reafilm/; the pre-rename "Celluloid"/"celluloid"
//  folders are read as fallbacks, see Persist.cpp)
bool saveKey(const std::string& key, std::string* err);
bool removeKey(std::string* err);

// Cached current status (loads + verifies once, re-checks when the file changes). Thread-safe.
LicenseInfo current();

// Feature gating helper: true if renders must carry the trial watermark.
inline bool needsWatermark(const LicenseInfo& li) { return !li.valid || li.edition == Edition::Trial; }

}} // namespace cel::lic
