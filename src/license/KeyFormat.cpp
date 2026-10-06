// Celluloid Pro - license key wire format. See KeyFormat.h for the byte layout.
#include "KeyFormat.h"
#include "../../third_party/monocypher/monocypher.h"
#include "../../third_party/monocypher/monocypher-ed25519.h"

#include <cstdio>
#include <cstring>

namespace cel { namespace lic { namespace fmt {

namespace {

const char kB32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int b32Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return 26 + (c - '2');
    return -1;
}

bool isAsciiSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// Length of an invisible/space-like UTF-8 sequence that commonly sneaks into pasted text
// (NBSP, narrow NBSP, zero-width space/non-joiner/joiner, word joiner, BOM), else 0.
size_t invisibleSeqLen(const std::string& s, size_t i) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data()) + i;
    const size_t left = s.size() - i;
    if (left >= 2 && p[0] == 0xC2 && p[1] == 0xA0) return 2;                       // U+00A0
    if (left >= 3 && p[0] == 0xE2 && p[1] == 0x80 &&
        (p[2] == 0x8B || p[2] == 0x8C || p[2] == 0x8D || p[2] == 0xAF)) return 3;   // U+200B..D, U+202F
    if (left >= 3 && p[0] == 0xE2 && p[1] == 0x81 && p[2] == 0xA0) return 3;        // U+2060
    if (left >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) return 3;        // U+FEFF
    return 0;
}

// Decodes one UTF-8 code point strictly (no overlongs, no surrogates, <= U+10FFFF).
bool nextCodePoint(const std::string& s, size_t& i, uint32_t& cp) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    size_t n;
    uint32_t minCp;
    if (c0 < 0x80) { cp = c0; i += 1; return true; }
    if ((c0 & 0xE0) == 0xC0) { n = 2; cp = c0 & 0x1F; minCp = 0x80; }
    else if ((c0 & 0xF0) == 0xE0) { n = 3; cp = c0 & 0x0F; minCp = 0x800; }
    else if ((c0 & 0xF8) == 0xF0) { n = 4; cp = c0 & 0x07; minCp = 0x10000; }
    else return false;
    if (i + n > s.size()) return false;
    for (size_t k = 1; k < n; ++k) {
        const unsigned char ck = static_cast<unsigned char>(s[i + k]);
        if ((ck & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (ck & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += n;
    return true;
}

bool isForbiddenCodePoint(uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F) return true;                 // C0 controls, DEL
    if (cp >= 0x80 && cp <= 0x9F) return true;                // C1 controls
    if (cp == 0x2028 || cp == 0x2029) return true;            // line / paragraph separator
    if (cp >= 0x202A && cp <= 0x202E) return true;            // bidi embeddings / overrides
    if (cp >= 0x2066 && cp <= 0x2069) return true;            // bidi isolates
    if (cp == 0x200E || cp == 0x200F || cp == 0x061C) return true;  // directional marks
    if (cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB)) return true; // BOM, interlinear anchors
    if (cp >= 0xFFFE && (cp & 0xFFFE) == 0xFFFE) return true; // non-characters U+xxFFFE/F
    return false;
}

void putU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int k = 0; k < 8; ++k) out.push_back(static_cast<uint8_t>(v >> (8 * k)));
}

struct Reader {
    const uint8_t* p;
    size_t n, pos = 0;
    bool u8(uint8_t& v) { if (pos + 1 > n) return false; v = p[pos++]; return true; }
    bool u16(uint32_t& v) {
        if (pos + 2 > n) return false;
        v = uint32_t(p[pos]) | (uint32_t(p[pos + 1]) << 8);
        pos += 2; return true;
    }
    bool i64(int64_t& v) {
        if (pos + 8 > n) return false;
        uint64_t u = 0;
        for (int k = 0; k < 8; ++k) u |= uint64_t(p[pos + size_t(k)]) << (8 * k);
        pos += 8;
        if (u > uint64_t(INT64_MAX)) return false;
        v = int64_t(u); return true;
    }
    bool str(std::string& s, size_t maxLen) {
        uint8_t len;
        if (!u8(len) || len == 0 || len > maxLen || pos + len > n) return false;
        s.assign(reinterpret_cast<const char*>(p + pos), len);
        pos += len; return true;
    }
};

bool setErr(std::string* err, const char* msg) { if (err) *err = msg; return false; }

std::string trimmedLine(const std::string& text, size_t b, size_t e) {
    while (b < e && isAsciiSpace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && isAsciiSpace(static_cast<unsigned char>(text[e - 1]))) --e;
    return text.substr(b, e - b);
}

// Finds the first non-comment line starting with `tag` and returns what follows it.
bool findTaggedLine(const std::string& text, const char* tag, std::string& value) {
    size_t b = 0;
    const size_t tagLen = std::strlen(tag);
    while (b <= text.size()) {
        size_t e = text.find('\n', b);
        if (e == std::string::npos) e = text.size();
        std::string line = trimmedLine(text, b, e);
        if (!line.empty() && line[0] != '#' && line.compare(0, tagLen, tag) == 0) {
            value = trimmedLine(line, tagLen, line.size());
            return true;
        }
        b = e + 1;
    }
    return false;
}

} // namespace

// ---- base32 ----------------------------------------------------------------------------------
std::string base32Encode(const uint8_t* data, size_t n) {
    std::string out;
    out.reserve((n * 8 + 4) / 5);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        buf = ((buf << 8) | data[i]) & 0xFFFu;
        bits += 8;
        while (bits >= 5) {
            out.push_back(kB32Alphabet[(buf >> (bits - 5)) & 31u]);
            bits -= 5;
        }
    }
    if (bits > 0) out.push_back(kB32Alphabet[(buf << (5 - bits)) & 31u]);
    return out;
}

bool base32Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    const size_t r = text.size() % 8;
    if (r == 1 || r == 3 || r == 6) return false;    // impossible unpadded lengths
    out.reserve(text.size() * 5 / 8);
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : text) {
        const int v = b32Value(c);
        if (v < 0) { out.clear(); return false; }
        buf = ((buf << 5) | uint32_t(v)) & 0xFFFu;
        bits += 5;
        if (bits >= 8) {
            out.push_back(static_cast<uint8_t>(buf >> (bits - 8)));
            bits -= 8;
        }
    }
    // canonical encoding only: fewer than 5 leftover bits, all zero
    if (bits >= 5 || (buf & ((1u << bits) - 1u)) != 0) { out.clear(); return false; }
    return true;
}

// ---- key text ---------------------------------------------------------------------------------
bool normalizeKeyText(const std::string& text, std::string& canonical) {
    canonical.clear();
    if (text.size() > kMaxKeyTextChars) return false;
    std::string compact;
    compact.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (isAsciiSpace(c)) { ++i; continue; }
        const size_t inv = invisibleSeqLen(text, i);
        if (inv) { i += inv; continue; }
        compact.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c));
        ++i;
    }
    const size_t plen = std::strlen(kKeyPrefix);
    if (compact.size() <= plen || compact.compare(0, plen, kKeyPrefix) != 0) return false;
    canonical = compact;
    return true;
}

// ---- payload ----------------------------------------------------------------------------------
bool validProduct(const std::string& s) {
    if (s.empty() || s.size() > kMaxProductBytes) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

bool validLicenseId(const std::string& s) {
    if (s.empty() || s.size() > kMaxIdBytes) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) return false;
    return true;
}

bool validLicensee(const std::string& s) {
    if (s.empty() || s.size() > kMaxLicenseeBytes) return false;
    if (s.front() == ' ' || s.back() == ' ') return false;
    bool visible = false;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        if (!nextCodePoint(s, i, cp)) return false;
        if (isForbiddenCodePoint(cp)) return false;
        if (cp != ' ' && cp != 0xA0 && cp != 0x3000 && !(cp >= 0x2000 && cp <= 0x200D)) visible = true;
    }
    return visible;
}

bool encodePayload(const Payload& p, std::vector<uint8_t>& out, std::string* err) {
    if (p.version != kPayloadVersion) return setErr(err, "unsupported payload version");
    const int ed = static_cast<int>(p.edition);
    if (ed < 0 || ed > 3) return setErr(err, "edition must be Trial, Indie, Pro or Studio");
    if (!validProduct(p.product)) return setErr(err, "product must be 1-32 chars [a-z0-9-]");
    if (!validLicensee(p.licensee))
        return setErr(err, "licensee must be 1-128 bytes of printable UTF-8 without leading/trailing spaces");
    if (!validLicenseId(p.id)) return setErr(err, "license id must be 1-64 chars [A-Za-z0-9._-]");
    if (p.issued <= 0 || p.issued >= kMaxTimestamp) return setErr(err, "issued timestamp out of range");
    if (p.expires != 0 && (p.expires <= p.issued || p.expires >= kMaxTimestamp))
        return setErr(err, "expires must be 0 (perpetual) or later than issued");
    if (p.seats < 1 || p.seats > 65535) return setErr(err, "seats must be 1..65535");
    out.push_back(p.version);
    out.push_back(static_cast<uint8_t>(ed));
    out.push_back(static_cast<uint8_t>(p.product.size()));
    out.insert(out.end(), p.product.begin(), p.product.end());
    out.push_back(static_cast<uint8_t>(p.licensee.size()));
    out.insert(out.end(), p.licensee.begin(), p.licensee.end());
    out.push_back(static_cast<uint8_t>(p.id.size()));
    out.insert(out.end(), p.id.begin(), p.id.end());
    putU64(out, static_cast<uint64_t>(p.issued));
    putU64(out, static_cast<uint64_t>(p.expires));
    out.push_back(static_cast<uint8_t>(p.seats & 0xFF));
    out.push_back(static_cast<uint8_t>(p.seats >> 8));
    return true;
}

bool decodePayload(const uint8_t* data, size_t n, Payload& p, std::string* err) {
    p = Payload();
    if (n < kMinPayloadBytes || n > kMaxPayloadBytes) return setErr(err, "payload size out of range");
    Reader r{data, n};
    uint8_t ver = 0, ed = 0;
    if (!r.u8(ver) || ver != kPayloadVersion) return setErr(err, "unsupported payload version");
    if (!r.u8(ed) || ed > 3) return setErr(err, "invalid edition");
    if (!r.str(p.product, kMaxProductBytes) || !validProduct(p.product)) return setErr(err, "invalid product");
    if (!r.str(p.licensee, kMaxLicenseeBytes) || !validLicensee(p.licensee)) return setErr(err, "invalid licensee");
    if (!r.str(p.id, kMaxIdBytes) || !validLicenseId(p.id)) return setErr(err, "invalid license id");
    if (!r.i64(p.issued) || p.issued <= 0 || p.issued >= kMaxTimestamp) return setErr(err, "invalid issue date");
    if (!r.i64(p.expires) || (p.expires != 0 && (p.expires <= p.issued || p.expires >= kMaxTimestamp)))
        return setErr(err, "invalid expiry date");
    if (!r.u16(p.seats) || p.seats < 1) return setErr(err, "invalid seat count");
    if (r.pos != n) return setErr(err, "trailing bytes in payload");
    p.version = ver;
    p.edition = static_cast<Edition>(ed);
    return true;
}

std::vector<uint8_t> signingMessage(const uint8_t* payload, size_t n) {
    std::vector<uint8_t> m;
    m.reserve(kSignDomainSize + n);
    m.insert(m.end(), reinterpret_cast<const uint8_t*>(kSignDomain),
             reinterpret_cast<const uint8_t*>(kSignDomain) + kSignDomainSize);
    m.insert(m.end(), payload, payload + n);
    return m;
}

// ---- misc -------------------------------------------------------------------------------------
const char* editionName(Edition e) {
    switch (e) {
        case Edition::Trial:  return "Trial";
        case Edition::Indie:  return "Indie";
        case Edition::Pro:    return "Pro";
        case Edition::Studio: return "Studio";
    }
    return "Unknown";
}

bool parseEdition(const std::string& s, Edition& e) {
    std::string l;
    for (char c : s) l.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
    if (l == "trial"  || l == "0") { e = Edition::Trial;  return true; }
    if (l == "indie"  || l == "1") { e = Edition::Indie;  return true; }
    if (l == "pro"    || l == "2") { e = Edition::Pro;    return true; }
    if (l == "studio" || l == "3") { e = Edition::Studio; return true; }
    return false;
}

// Howard Hinnant's public-domain civil-date algorithms (proleptic Gregorian).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
}

std::string formatDateUtc(int64_t t) {
    int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    int64_t y; unsigned m, d;
    civilFromDays(days, y, m, d);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return buf;
}

bool parseDateUtc(const std::string& s, int64_t& out) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (s[i] < '0' || s[i] > '9') return false;
    const int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    const unsigned m = unsigned((s[5] - '0') * 10 + (s[6] - '0'));
    const unsigned d = unsigned((s[8] - '0') * 10 + (s[9] - '0'));
    if (y < 1970 || m < 1 || m > 12 || d < 1) return false;
    static const unsigned mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const unsigned dim = mdays[m - 1] + (m == 2 && leap ? 1u : 0u);
    if (d > dim) return false;
    out = daysFromCivil(y, m, d) * 86400;
    return true;
}

// Data-independent hex: no table lookups or branches indexed by secret nibbles.
std::string hexEncode(const uint8_t* data, size_t n) {
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        const uint32_t hi = data[i] >> 4, lo = data[i] & 15u;
        s[2 * i]     = static_cast<char>(hi + 48u + (((9u - hi) >> 8) & 39u));
        s[2 * i + 1] = static_cast<char>(lo + 48u + (((9u - lo) >> 8) & 39u));
    }
    return s;
}

bool hexDecode(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != 2 * n) return false;
    uint32_t bad = 0;
    for (size_t i = 0; i < 2 * n; ++i) {
        const uint32_t c = static_cast<unsigned char>(s[i]);
        const uint32_t num = c ^ 48u;                        // '0'..'9' -> 0..9
        const uint32_t isNum = ((num - 10u) >> 8) & 1u;      // 1 if num < 10
        const uint32_t alpha = (c | 32u) - 87u;              // 'a'..'f' / 'A'..'F' -> 10..15
        const uint32_t isAlpha = (((alpha - 10u) ^ (alpha - 16u)) >> 8) & 1u;   // 1 if 10 <= alpha < 16
        const uint32_t val = (num & (0u - isNum)) | (alpha & (0u - isAlpha));
        bad |= (isNum | isAlpha) ^ 1u;
        const uint32_t nib = val & 15u;
        if (i & 1) out[i / 2] = static_cast<uint8_t>(out[i / 2] | nib);
        else       out[i / 2] = static_cast<uint8_t>(nib << 4);
    }
    if (bad) { secureWipe(out, n); return false; }
    return true;
}

std::string publicKeyFileText(const uint8_t pub[kPublicKeySize], const std::string& comment) {
    std::string t;
    if (!comment.empty()) t += "# " + comment + "\n";
    t += "# Ed25519 public key (RFC 8032). Fingerprint: " + keyFingerprint(pub) + "\n";
    t += "CELPUB1:" + hexEncode(pub, kPublicKeySize) + "\n";
    return t;
}

std::string privateKeyFileText(const uint8_t seed[kSeedSize], const std::string& comment) {
    std::string t;
    if (!comment.empty()) t += "# " + comment + "\n";
    t += "# Ed25519 PRIVATE signing key (RFC 8032 seed). Anyone holding this file can issue\n"
         "# Celluloid Pro licenses. Keep it offline, backed up, and out of every repository.\n";
    t += "CELPRIV1:" + hexEncode(seed, kSeedSize) + "\n";
    return t;
}

bool parsePublicKeyFile(const std::string& text, uint8_t pub[kPublicKeySize]) {
    std::string v;
    return findTaggedLine(text, "CELPUB1:", v) && hexDecode(v, pub, kPublicKeySize);
}

bool parsePrivateKeyFile(const std::string& text, uint8_t seed[kSeedSize]) {
    std::string v;
    const bool ok = findTaggedLine(text, "CELPRIV1:", v) && hexDecode(v, seed, kSeedSize);
    secureWipe(&v[0], v.size());
    return ok;
}

std::string keyFingerprint(const uint8_t pub[kPublicKeySize]) {
    uint8_t h[64];
    crypto_sha512(h, pub, kPublicKeySize);
    return hexEncode(h, 8);
}

void secureWipe(void* p, size_t n) {
    if (p && n) crypto_wipe(p, n);
}

}}} // namespace cel::lic::fmt
