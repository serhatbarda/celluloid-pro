// Reafilm Pro - license key wire format (shared by the plugin, tools/licensegen and the tests).
//
// A license key is the text
//
//     CELL1-<BASE32( payload || signature )>
//
// BASE32 is RFC 4648 (alphabet A-Z 2-7), unpadded, canonical (unused trailing bits are zero).
// Decoding is case-insensitive and ignores whitespace (keys wrap in e-mails), nothing else.
//
// payload (format version 1, integers little-endian):
//
//     off  size  field
//     0    1     version            = 0x01
//     1    1     edition            0 Trial, 1 Indie, 2 Pro, 3 Studio
//     2    1+n   product            u8 length (1..32) + bytes, [a-z0-9-], must be "celluloid-pro"
//     .    1+n   licensee           u8 length (1..128) + UTF-8 (no control characters)
//     .    1+n   license id         u8 length (1..64)  + bytes, [A-Za-z0-9._-]
//     .    8     issued             int64 Unix seconds (UTC), 1 .. 2^40
//     .    8     expires            int64 Unix seconds (UTC), 0 = perpetual, else > issued
//     .    2     seats              u16, 1 .. 65535
//
// signature = Ed25519 (RFC 8032, SHA-512) over  kSignDomain (including its terminating NUL)
// followed by the payload bytes.  The domain prefix makes the signatures useless for anything
// else that might ever be signed with the same key.
//
// Any implementation of standard Ed25519 can issue keys (see tools/licensegen/README.md and the
// reference Python implementation tools/licensegen/celluloid_license.py).
#pragma once
#include "License.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cel { namespace lic { namespace fmt {

constexpr const char* kKeyPrefix      = "CELL1-";
constexpr const char* kProduct        = "celluloid-pro";
constexpr uint8_t     kPayloadVersion = 1;
// Signed-message domain separator. The NUL terminator is part of the signed message.
constexpr const char  kSignDomain[]   = "CelluloidPro/license-key/v1";
constexpr size_t      kSignDomainSize = sizeof(kSignDomain);   // includes the NUL

constexpr size_t kSignatureSize  = 64;
constexpr size_t kPublicKeySize  = 32;
constexpr size_t kSeedSize       = 32;   // Ed25519 private key (RFC 8032 "secret key")

constexpr size_t kMaxProductBytes  = 32;
constexpr size_t kMaxLicenseeBytes = 128;
constexpr size_t kMaxIdBytes       = 64;
constexpr size_t kMinPayloadBytes  = 1 + 1 + (1 + 1) + (1 + 1) + (1 + 1) + 8 + 8 + 2;   // 26
constexpr size_t kMaxPayloadBytes  = 1 + 1 + (1 + kMaxProductBytes) + (1 + kMaxLicenseeBytes) +
                                     (1 + kMaxIdBytes) + 8 + 8 + 2;                       // 246
constexpr size_t kMaxKeyTextChars  = 4096;   // raw user input, incl. whitespace
constexpr int64_t kMaxTimestamp    = int64_t(1) << 40;   // ~ year 36812; sanity bound only

// Clock-skew tolerance applied to "issued" (key from the future) and "expires".
constexpr int64_t kClockSkewSeconds = 48 * 3600;

struct Payload {
    uint8_t     version  = kPayloadVersion;
    Edition     edition  = Edition::Pro;
    std::string product  = kProduct;
    std::string licensee;
    std::string id;
    int64_t     issued   = 0;
    int64_t     expires  = 0;          // 0 = perpetual
    uint32_t    seats    = 1;
};

// ---- base32 (RFC 4648, unpadded) ------------------------------------------------------------
std::string base32Encode(const uint8_t* data, size_t n);
// Strict: only [A-Za-z2-7], length must be a valid unpadded length, unused bits must be zero.
bool base32Decode(const std::string& text, std::vector<uint8_t>& out);

// ---- key text -------------------------------------------------------------------------------
// Removes ASCII whitespace, NBSP, zero-width space/joiners and BOMs; upper-cases the body.
// Returns false if the result does not start with the CELL1- prefix (case-insensitive) or the
// input is absurdly long. On success `canonical` = "CELL1-" + upper-case body.
bool normalizeKeyText(const std::string& text, std::string& canonical);

// ---- payload --------------------------------------------------------------------------------
bool validProduct(const std::string& s);
bool validLicensee(const std::string& s);     // UTF-8, 1..128 bytes, no controls, not blank
bool validLicenseId(const std::string& s);
// Checks every field; appends the binary payload to `out`.
bool encodePayload(const Payload& p, std::vector<uint8_t>& out, std::string* err);
// Strict decode: every field validated, no trailing bytes.
bool decodePayload(const uint8_t* data, size_t n, Payload& p, std::string* err);
// kSignDomain (with NUL) || payload
std::vector<uint8_t> signingMessage(const uint8_t* payload, size_t n);

// ---- misc helpers ---------------------------------------------------------------------------
const char* editionName(Edition e);                     // "Trial", "Indie", "Pro", "Studio"
bool parseEdition(const std::string& s, Edition& e);    // case-insensitive names or 0..3

// Proleptic Gregorian calendar, UTC (no time-zone database, no 2038 problem).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d);
void    civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d);
std::string formatDateUtc(int64_t unixSeconds);          // "YYYY-MM-DD"
// "YYYY-MM-DD" (00:00:00 UTC) -> unix seconds. Strict (validates day of month).
bool parseDateUtc(const std::string& s, int64_t& unixSeconds);

// Hex with data-independent timing (used for private-key files).
std::string hexEncode(const uint8_t* data, size_t n);
bool hexDecode(const std::string& s, uint8_t* out, size_t n);   // exactly 2n hex digits

// Key-file helpers: "CELPUB1:<hex>" / "CELPRIV1:<hex>" lines; '#' lines are comments.
std::string publicKeyFileText(const uint8_t pub[kPublicKeySize], const std::string& comment);
std::string privateKeyFileText(const uint8_t seed[kSeedSize], const std::string& comment);
bool parsePublicKeyFile(const std::string& text, uint8_t pub[kPublicKeySize]);
bool parsePrivateKeyFile(const std::string& text, uint8_t seed[kSeedSize]);

// 16 hex digits of SHA-512(public key), for humans ("which key is this build using?").
std::string keyFingerprint(const uint8_t pub[kPublicKeySize]);

// Overwrite memory that held secrets (not optimised away).
void secureWipe(void* p, size_t n);

}}} // namespace cel::lic::fmt
