// Celluloid Pro - licensing internals: trusted-key selection, key-file locations and test hooks.
// Not part of the fixed interface (License.h); used by the OFX shell (paths for messages),
// tools/licensegen and tests/license.
#pragma once
#include "License.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace cel { namespace lic { namespace detail {

struct TrustedKey {
    const uint8_t* pub;   // 32-byte Ed25519 public key
    bool           dev;   // development key (message is tagged "[DEVELOPMENT KEY]")
};

// verifyKey() against an explicit set of trusted public keys (verifyKey uses the compiled-in set).
LicenseInfo verifyKeyWithKeys(const std::string& key, int64_t nowUnix, const TrustedKey* keys, size_t count);

// True when this build was configured with -DCEL_DEV_LICENSE=ON (accepts the development key).
bool devKeysAccepted();
// Comma-separated fingerprints of the compiled-in production keys (for About/diagnostics).
std::string productionKeyFingerprints();
// True when the compiled-in production key is the provisional one from the development box.
bool productionKeyIsProvisional();

// Canonical form of a key ("CELL1-" + upper-case body without whitespace); empty if malformed.
std::string canonicalKey(const std::string& key);

// Key file locations (UTF-8). Empty string if the location cannot be determined.
std::string userKeyFilePath();
std::string machineKeyFilePath();
// Environment variable that may hold a key (render farms): CELLULOID_LICENSE_KEY.
constexpr const char* kLicenseEnvVar = "CELLULOID_LICENSE_KEY";

// Drops the cached status; the next current() reloads everything.
void invalidateCache();

// ---- test hooks (not for production code) ----------------------------------------------------
// Override the key file paths. Both empty = platform defaults; otherwise an empty path means
// "no such file" (e.g. no machine-wide file). Also invalidates the cache.
void setKeyPathsForTesting(const std::string& userFile, const std::string& machineFile);
// Minimum interval between file re-checks in current() (default 1500 ms).
void setRecheckIntervalForTesting(int milliseconds);
// Use a fixed clock for current()/saveKey() (0 = system clock). Does not invalidate the cache.
void setClockForTesting(int64_t fixedNowUnix);

}}} // namespace cel::lic::detail
