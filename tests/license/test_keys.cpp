// tests/license - key format, signatures, verification policy.
#include "TestUtil.h"
#include "../../src/license/License.h"
#include "../../src/license/KeyFormat.h"
#include "../../src/license/KeySigner.h"
#include "../../src/license/LicenseInternal.h"
#include "../../third_party/monocypher/monocypher.h"
#include "../../third_party/monocypher/monocypher-ed25519.h"

#include <chrono>
#include <ctime>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef CEL_LICENSE_REPO_ROOT
#error "CEL_LICENSE_REPO_ROOT must point at the repository root (for the development key pair)"
#endif

using namespace cel::lic;

namespace {

const int64_t kNow = 1791504000;   // 2026-10-09 00:00:00 UTC

std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> v(h.size() / 2);
    for (size_t i = 0; i < v.size(); ++i) v[i] = uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16));
    return v;
}

struct TestKeyPair {
    uint8_t seed[32], pub[32];
    explicit TestKeyPair(uint8_t fill) {
        for (int i = 0; i < 32; ++i) seed[i] = uint8_t(fill * 31 + i * 7 + 1);
        sign::derivePublicKey(seed, pub);
    }
    detail::TrustedKey trusted(bool dev = false) const { return {pub, dev}; }
};

fmt::Payload basePayload() {
    fmt::Payload p;
    p.edition = Edition::Pro;
    p.licensee = "Jane Colourist <jane@example.com>";
    p.id = "CP-TEST-0001";
    p.issued = kNow - 86400 * 30;
    p.expires = 0;
    p.seats = 1;
    return p;
}

std::string signOrDie(const fmt::Payload& p, const TestKeyPair& kp) {
    std::string key, err;
    const bool ok = sign::signLicenseKey(p, kp.seed, key, &err);
    CHECK_MSG(ok, err);
    return key;
}

// Signs arbitrary payload bytes (bypassing encodePayload's validation) -> key text.
std::string signRaw(const std::vector<uint8_t>& payload, const TestKeyPair& kp) {
    const std::vector<uint8_t> msg = fmt::signingMessage(payload.data(), payload.size());
    uint8_t sk[64], pub[32], s[32], sig[64];
    std::memcpy(s, kp.seed, 32);
    crypto_ed25519_key_pair(sk, pub, s);
    crypto_ed25519_sign(sig, sk, msg.data(), msg.size());
    std::vector<uint8_t> blob = payload;
    blob.insert(blob.end(), sig, sig + 64);
    return std::string(fmt::kKeyPrefix) + fmt::base32Encode(blob.data(), blob.size());
}

LicenseInfo verifyWith(const std::string& key, const TestKeyPair& kp, int64_t now = kNow) {
    const detail::TrustedKey t = kp.trusted();
    return detail::verifyKeyWithKeys(key, now, &t, 1);
}

bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ---- Monocypher / Ed25519 ---------------------------------------------------------------------
CEL_TEST(ed25519_rfc8032_vectors) {
    struct V { const char *sk, *pk, *msg, *sig; };
    const V vs[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    for (const V& v : vs) {
        std::vector<uint8_t> seed = unhex(v.sk), pk = unhex(v.pk), msg = unhex(v.msg), sig = unhex(v.sig);
        uint8_t pub[32], sk[64], s[64];
        sign::derivePublicKey(seed.data(), pub);
        CHECK(std::memcmp(pub, pk.data(), 32) == 0);
        crypto_ed25519_key_pair(sk, pub, seed.data());
        crypto_ed25519_sign(s, sk, msg.data(), msg.size());
        CHECK(std::memcmp(s, sig.data(), 64) == 0);
        CHECK(crypto_ed25519_check(sig.data(), pk.data(), msg.data(), msg.size()) == 0);
        sig[5] ^= 0x10;
        CHECK(crypto_ed25519_check(sig.data(), pk.data(), msg.data(), msg.size()) != 0);
    }
}

// ---- base32 -------------------------------------------------------------------------------------
CEL_TEST(base32_rfc4648_vectors_and_strictness) {
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* enc[] = {"", "MY", "MZXQ", "MZXW6", "MZXW6YQ", "MZXW6YTB", "MZXW6YTBOI"};
    for (int i = 0; i < 7; ++i) {
        const std::string p = plain[i];
        CHECK(fmt::base32Encode(reinterpret_cast<const uint8_t*>(p.data()), p.size()) == enc[i]);
        std::vector<uint8_t> d;
        CHECK(fmt::base32Decode(enc[i], d) && std::string(d.begin(), d.end()) == p);
        std::string lower = enc[i];
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        CHECK(fmt::base32Decode(lower, d) && std::string(d.begin(), d.end()) == p);
    }
    std::vector<uint8_t> d;
    CHECK(!fmt::base32Decode("M", d));             // impossible lengths 1, 3, 6 (mod 8)
    CHECK(!fmt::base32Decode("MZX", d));
    CHECK(!fmt::base32Decode("MZXW6Y", d));
    CHECK(!fmt::base32Decode("MZXW6YTBM", d));
    CHECK(!fmt::base32Decode("M3", d));            // non-canonical: unused bits not zero
    CHECK(!fmt::base32Decode("MZXR", d));
    CHECK(!fmt::base32Decode("MZXW7", d));
    CHECK(!fmt::base32Decode("MZ0Q", d));          // characters outside the alphabet
    CHECK(!fmt::base32Decode("MZ1Q", d));
    CHECK(!fmt::base32Decode("MZ8Q", d));
    CHECK(!fmt::base32Decode("MY======", d));       // no padding
    CHECK(!fmt::base32Decode("MZ-Q", d));
    CHECK(!fmt::base32Decode(std::string("MZ\0Q", 4), d));
    CHECK(!fmt::base32Decode("MZ\xc3\x84Q", d));    // UTF-8 letter
    // random round trips across all lengths
    std::mt19937 rng(42);
    for (size_t n = 0; n < 400; ++n) {
        std::vector<uint8_t> v(n);
        for (auto& b : v) b = uint8_t(rng());
        const std::string e = fmt::base32Encode(v.data(), v.size());
        CHECK(e.size() == (n * 8 + 4) / 5);
        std::vector<uint8_t> back;
        CHECK(fmt::base32Decode(e, back) && back == v);
    }
}

CEL_TEST(key_text_normalisation) {
    std::string c;
    CHECK(fmt::normalizeKeyText("  cell1-abcd\r\n efgh\t", c) && c == "CELL1-ABCDEFGH");
    CHECK(fmt::normalizeKeyText("CELL1-AB\xc2\xa0" "CD\xe2\x80\x8b" "EF\xef\xbb\xbf", c) && c == "CELL1-ABCDEF");
    CHECK(!fmt::normalizeKeyText("CELL2-ABCD", c));
    CHECK(!fmt::normalizeKeyText("ABCD", c));
    CHECK(!fmt::normalizeKeyText("CELL1-", c));
    CHECK(!fmt::normalizeKeyText(std::string(fmt::kMaxKeyTextChars + 1, ' '), c));
}

// ---- helpers -------------------------------------------------------------------------------------
CEL_TEST(hex_dates_keyfiles) {
    uint8_t b[32], back[32];
    for (int i = 0; i < 32; ++i) b[i] = uint8_t(i * 37 + 3);
    const std::string h = fmt::hexEncode(b, 32);
    CHECK(h.size() == 64);
    CHECK(fmt::hexDecode(h, back, 32) && std::memcmp(b, back, 32) == 0);
    std::string upper = h;
    for (char& ch : upper) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    CHECK(fmt::hexDecode(upper, back, 32) && std::memcmp(b, back, 32) == 0);
    for (const char* bad : {"0g", "g0", "0:", "/0", "0@", "`0", "0G", " 0"}) {
        uint8_t o[1];
        CHECK(!fmt::hexDecode(bad, o, 1));
    }
    for (int v = 0; v < 256; ++v) {
        uint8_t in = uint8_t(v), out = 0;
        CHECK(fmt::hexDecode(fmt::hexEncode(&in, 1), &out, 1) && out == in);
    }
    uint8_t one;
    CHECK(!fmt::hexDecode("abc", &one, 1));

    int64_t t;
    CHECK(fmt::parseDateUtc("1970-01-01", t) && t == 0);
    CHECK(fmt::parseDateUtc("2026-10-09", t) && t == kNow);
    CHECK(fmt::parseDateUtc("2028-02-29", t));
    CHECK(fmt::parseDateUtc("2000-02-29", t));
    CHECK(!fmt::parseDateUtc("2100-02-29", t));
    CHECK(!fmt::parseDateUtc("2026-02-29", t));
    CHECK(!fmt::parseDateUtc("2026-13-01", t));
    CHECK(!fmt::parseDateUtc("2026-04-31", t));
    CHECK(!fmt::parseDateUtc("2026-1-01", t));
    CHECK(!fmt::parseDateUtc("1969-12-31", t));
    CHECK(fmt::formatDateUtc(0) == "1970-01-01");
    CHECK(fmt::formatDateUtc(kNow + 86399) == "2026-10-09");
    CHECK(fmt::formatDateUtc(int64_t(253402300799)) == "9999-12-31");
    for (int64_t day = -1000; day < 200000; day += 37) {
        int64_t y; unsigned m, d;
        fmt::civilFromDays(day, y, m, d);
        CHECK(fmt::daysFromCivil(y, m, d) == day);
    }

    TestKeyPair kp(9);
    uint8_t pub[32], seed[32];
    CHECK(fmt::parsePublicKeyFile(fmt::publicKeyFileText(kp.pub, "c"), pub) && std::memcmp(pub, kp.pub, 32) == 0);
    CHECK(fmt::parsePrivateKeyFile(fmt::privateKeyFileText(kp.seed, "c"), seed) && std::memcmp(seed, kp.seed, 32) == 0);
    CHECK(!fmt::parsePrivateKeyFile(fmt::publicKeyFileText(kp.pub, "c"), seed));
    CHECK(!fmt::parsePublicKeyFile("# CELPUB1:" + fmt::hexEncode(kp.pub, 32) + "\n", pub));   // commented out
    CHECK(fmt::keyFingerprint(kp.pub).size() == 16);

    std::string id;
    CHECK(sign::randomLicenseId(id) && id.size() == 19 && fmt::validLicenseId(id));
    std::string id2;
    CHECK(sign::randomLicenseId(id2) && id2 != id);
}

// ---- payload validation ------------------------------------------------------------------------
CEL_TEST(payload_field_validation) {
    CHECK(fmt::validLicensee("Jane Doe"));
    CHECK(fmt::validLicensee("Zo\xc3\xab \xc3\x98ster \xe6\x98\xa0\xe7\x94\xbb"));   // Zoë Øster 映画
    CHECK(fmt::validLicensee("Studio K\xc3\xb6ln GmbH <licensing@example.de>"));
    CHECK(!fmt::validLicensee(""));
    CHECK(!fmt::validLicensee(" Jane"));
    CHECK(!fmt::validLicensee("Jane "));
    CHECK(!fmt::validLicensee("   "));
    CHECK(!fmt::validLicensee("Jane\nDoe"));
    CHECK(!fmt::validLicensee("Jane\x7f"));
    CHECK(!fmt::validLicensee("Jane\xc2\x85"));               // C1 NEL
    CHECK(!fmt::validLicensee("evil\xe2\x80\xaegnp.exe"));     // RIGHT-TO-LEFT OVERRIDE
    CHECK(!fmt::validLicensee("\xc0\xaf"));                    // overlong '/'
    CHECK(!fmt::validLicensee("\xed\xa0\x80"));                // surrogate
    CHECK(!fmt::validLicensee("\xf4\x90\x80\x80"));            // > U+10FFFF
    CHECK(!fmt::validLicensee("abc\xe6\x98"));                 // truncated sequence
    CHECK(fmt::validLicensee(std::string(128, 'a')));
    CHECK(!fmt::validLicensee(std::string(129, 'a')));
    CHECK(fmt::validLicenseId("ORDER-2026.10_77"));
    CHECK(!fmt::validLicenseId("ORDER 1"));
    CHECK(!fmt::validLicenseId(std::string(65, 'A')));
    CHECK(fmt::validProduct("celluloid-pro"));
    CHECK(!fmt::validProduct("Celluloid-Pro"));

    fmt::Payload p = basePayload();
    std::vector<uint8_t> out;
    std::string err;
    CHECK(fmt::encodePayload(p, out, &err));
    fmt::Payload q;
    CHECK(fmt::decodePayload(out.data(), out.size(), q, &err));
    CHECK(q.licensee == p.licensee && q.id == p.id && q.issued == p.issued && q.seats == 1 && q.edition == Edition::Pro);
    std::vector<uint8_t> longer = out;
    longer.push_back(0);
    CHECK(!fmt::decodePayload(longer.data(), longer.size(), q, &err));
    CHECK(!fmt::decodePayload(out.data(), out.size() - 1, q, &err));

    fmt::Payload bad = p; bad.seats = 0;            out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
    bad = p; bad.seats = 70000;                     out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
    bad = p; bad.expires = p.issued;                out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
    bad = p; bad.issued = 0;                        out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
    bad = p; bad.edition = static_cast<Edition>(4); out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
    bad = p; bad.id = "";                           out.clear(); CHECK(!fmt::encodePayload(bad, out, &err));
}

// ---- verification policy -----------------------------------------------------------------------
CEL_TEST(verify_valid_keys) {
    TestKeyPair kp(1);
    const std::string key = signOrDie(basePayload(), kp);
    std::printf("    example key (%zu chars): %s\n", key.size(), key.c_str());
    LicenseInfo li = verifyWith(key, kp);
    CHECK_MSG(li.valid, li.message);
    CHECK(li.edition == Edition::Pro && li.licensee == "Jane Colourist <jane@example.com>");
    CHECK(li.licenseId == "CP-TEST-0001" && li.seats == 1 && li.expiresUnix == 0);
    CHECK(!needsWatermark(li));
    CHECK(contains(li.message, "Licensed to Jane Colourist") && contains(li.message, "Pro"));
    CHECK(!contains(li.message, "DEVELOPMENT"));

    // pasted from an e-mail: wrapped, lower case, NBSP
    std::string wrapped;
    for (size_t i = 0; i < key.size(); ++i) {
        wrapped.push_back(char(std::tolower(static_cast<unsigned char>(key[i]))));
        if (i % 60 == 59) wrapped += "\r\n   ";
    }
    wrapped = "\xc2\xa0 " + wrapped + "\n";
    CHECK(verifyWith(wrapped, kp).valid);
    CHECK(detail::canonicalKey(wrapped) == key);

    // studio, seats, unicode, maximum field sizes
    fmt::Payload p = basePayload();
    p.edition = Edition::Studio;
    p.seats = 25;
    p.licensee = "Zo\xc3\xab \xc3\x98ster \xe6\x98\xa0\xe7\x94\xbb";
    li = verifyWith(signOrDie(p, kp), kp);
    CHECK(li.valid && li.edition == Edition::Studio && li.seats == 25 && li.licensee == p.licensee);
    CHECK(contains(li.message, "25 seats"));
    p.licensee = std::string(128, 'L');
    p.id = std::string(64, 'I');
    const std::string big = signOrDie(p, kp);
    std::printf("    largest key: %zu chars\n", big.size());
    CHECK(verifyWith(big, kp).valid);

    // a Trial-edition key is valid but keeps the watermark
    p = basePayload();
    p.edition = Edition::Trial;
    li = verifyWith(signOrDie(p, kp), kp);
    CHECK(li.valid && needsWatermark(li));

    // indie, edition names
    p.edition = Edition::Indie;
    li = verifyWith(signOrDie(p, kp), kp);
    CHECK(li.valid && li.edition == Edition::Indie && contains(li.message, "Indie"));
}

CEL_TEST(verify_dates_and_clock_skew) {
    TestKeyPair kp(2);
    fmt::Payload p = basePayload();
    p.issued = kNow;
    p.expires = kNow + 30 * 86400;
    const std::string key = signOrDie(p, kp);
    LicenseInfo li = verifyWith(key, kp, kNow + 86400);
    CHECK(li.valid && contains(li.message, "valid until " + fmt::formatDateUtc(p.expires - 1)));
    CHECK(verifyWith(key, kp, p.expires + 3600).valid);                                 // within skew
    li = verifyWith(key, kp, p.expires + fmt::kClockSkewSeconds + 1);
    CHECK(!li.valid && contains(li.message, "expired") && needsWatermark(li));
    CHECK(li.licensee == p.licensee);                                                   // still identified
    CHECK(verifyWith(key, kp, kNow - fmt::kClockSkewSeconds + 60).valid);              // clock slightly behind
    li = verifyWith(key, kp, kNow - fmt::kClockSkewSeconds - 60);
    CHECK(!li.valid && contains(li.message, "not valid yet"));
    li = verifyWith(key, kp, 0);                                                        // clock unknown
    CHECK(!li.valid && contains(li.message, "clock"));
    p.expires = 0;
    CHECK(verifyWith(signOrDie(p, kp), kp, 0).valid);                                  // perpetual: fine
    CHECK(verifyWith(signOrDie(p, kp), kp, int64_t(4102444800)).valid);                // year 2100
}

CEL_TEST(verify_rejects_wrong_product_key_and_format) {
    TestKeyPair kp(3), other(4);
    fmt::Payload p = basePayload();
    p.product = "celluloid-lite";
    LicenseInfo li = verifyWith(signOrDie(p, kp), kp);
    CHECK(!li.valid && contains(li.message, "different product"));

    const std::string key = signOrDie(basePayload(), kp);
    li = verifyWith(key, other);
    CHECK(!li.valid && contains(li.message, "signature"));
    CHECK(detail::verifyKeyWithKeys(key, kNow, nullptr, 0).valid == false);
    // first matching key of several wins
    const detail::TrustedKey both[2] = {other.trusted(), kp.trusted(true)};
    li = detail::verifyKeyWithKeys(key, kNow, both, 2);
    CHECK(li.valid && contains(li.message, "[DEVELOPMENT KEY]"));

    CHECK(contains(verifyWith("", kp).message, "No license key"));
    CHECK(contains(verifyWith(" \n\t", kp).message, "No license key"));
    CHECK(contains(verifyWith("hello", kp).message, "CELL1-"));
    CHECK(contains(verifyWith("CELL2-" + key.substr(6), kp).message, "CELL1-"));
    li = verifyWith(key.substr(0, key.size() - 1), kp);
    CHECK(!li.valid && (contains(li.message, "damaged") || contains(li.message, "signature")));
    CHECK(contains(verifyWith(key.substr(0, 60), kp).message, "damaged"));
    CHECK(!verifyWith(key.substr(0, key.size() - 8), kp).valid);
    CHECK(!verifyWith(key + "A", kp).valid);
    CHECK(!verifyWith(key + "AAAAAAAA", kp).valid);
    CHECK(!verifyWith(key + "=", kp).valid);
    CHECK(!verifyWith(key + ".", kp).valid);
    CHECK(!verifyWith("CELL1-" + std::string(100, 'A'), kp).valid);
    CHECK(!verifyWith(std::string(1 << 20, 'A'), kp).valid);                            // 1 MiB
    CHECK(!verifyWith("CELL1-" + std::string(1 << 20, 'A'), kp).valid);
    CHECK(!verifyWith(std::string("CELL1-AB\0CD", 11), kp).valid);

    // a future format version is reported as such
    std::vector<uint8_t> raw;
    CHECK(fmt::base32Decode(key.substr(6), raw));
    raw[0] = 2;
    li = verifyWith("CELL1-" + fmt::base32Encode(raw.data(), raw.size()), kp);
    CHECK(!li.valid && contains(li.message, "newer version"));
}

CEL_TEST(verify_rejects_every_single_bit_flip) {
    TestKeyPair kp(5);
    const std::string key = signOrDie(basePayload(), kp);
    std::vector<uint8_t> raw;
    CHECK(fmt::base32Decode(key.substr(6), raw));
    int accepted = 0;
    for (size_t i = 0; i < raw.size(); ++i)
        for (int b = 0; b < 8; ++b) {
            std::vector<uint8_t> t = raw;
            t[i] ^= uint8_t(1 << b);
            if (verifyWith("CELL1-" + fmt::base32Encode(t.data(), t.size()), kp).valid) ++accepted;
        }
    CHECK_MSG(accepted == 0, std::to_string(accepted) + " tampered keys accepted");
    // every single-character substitution in the text
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    accepted = 0;
    for (size_t i = 6; i < key.size(); ++i) {
        std::string t = key;
        t[i] = alphabet[(std::strchr(alphabet, key[i]) - alphabet + 1 + int(i) % 31) % 32];
        if (verifyWith(t, kp).valid) ++accepted;
    }
    CHECK(accepted == 0);
}

CEL_TEST(verify_rejects_malformed_signed_payloads) {
    // Payloads that are correctly signed but violate the format must still be rejected.
    TestKeyPair kp(6);
    std::vector<uint8_t> good;
    std::string err;
    CHECK(fmt::encodePayload(basePayload(), good, &err));
    CHECK(verifyWith(signRaw(good, kp), kp).valid);

    std::vector<uint8_t> t = good; t.push_back(0);                      // trailing byte
    CHECK(contains(verifyWith(signRaw(t, kp), kp).message, "not supported"));
    t = good; t[1] = 7;                                                 // edition out of range
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    const size_t licOff = 2 + 1 + std::strlen(fmt::kProduct);
    t = good; t[licOff + 1] = '\n';                                    // control char in licensee
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t = good; t[licOff + 1] = 0xC0;                                    // invalid UTF-8
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t = good; t[licOff] = 200;                                         // length beyond the data
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t = good; t[t.size() - 2] = 0; t[t.size() - 1] = 0;                // seats = 0
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t = good;                                                           // expires < issued
    const size_t expOff = t.size() - 2 - 8;
    t[expOff] = 1; for (int k = 1; k < 8; ++k) t[expOff + size_t(k)] = 0;
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t = good;                                                           // negative issued (int64 sign bit)
    t[expOff - 1] = 0x80;
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
    t.assign(good.begin(), good.begin() + 20);                         // truncated
    CHECK(!verifyWith(signRaw(t, kp), kp).valid);
}

CEL_TEST(verify_random_mutation_fuzz) {
    TestKeyPair kp(7);
    const std::string key = signOrDie(basePayload(), kp);
    std::mt19937 rng(1234);
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz234567 -=\n\x01\xc2\xa0\xff";
    const size_t nchars = std::strlen(chars);
    int falsePositives = 0, iterations = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < 30000; ++it) {
        std::string t = key;
        const int edits = 1 + int(rng() % 4);
        for (int e = 0; e < edits; ++e) {
            const size_t pos = rng() % (t.size() + 1);
            switch (rng() % 3) {
                case 0: if (pos < t.size()) t[pos] = chars[rng() % nchars]; break;
                case 1: t.insert(t.begin() + long(pos), chars[rng() % nchars]); break;
                default: if (pos < t.size()) t.erase(pos, 1); break;
            }
        }
        const LicenseInfo li = verifyWith(t, kp);
        if (li.valid && detail::canonicalKey(t) != key) ++falsePositives;
        CHECK(!li.message.empty());
        ++iterations;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    %d mutated keys in %.0f ms, %d accepted wrongly\n", iterations, ms, falsePositives);
    CHECK(falsePositives == 0);
}

// ---- compiled-in keys -----------------------------------------------------------------------------
CEL_TEST(compiled_in_keys_and_dev_isolation) {
    uint8_t devSeed[32];
    const std::string devFile = std::string(CEL_LICENSE_REPO_ROOT) + "/tools/licensegen/dev-keys/celluloid-dev.private.key";
    const bool haveDevSeed = fmt::parsePrivateKeyFile(readText(devFile), devSeed);
    CHECK_MSG(haveDevSeed, "cannot read " + devFile);
    if (!haveDevSeed) return;
    fmt::Payload p = basePayload();
    p.licensee = "Reafilm Developer";
    std::string key, err;
    CHECK(sign::signLicenseKey(p, devSeed, key, &err));
    const LicenseInfo li = verifyKey(key, kNow);
    std::printf("    dev key in this build (%s): %s\n", detail::devKeysAccepted() ? "CEL_DEV_LICENSE=ON" : "release",
                li.message.c_str());
    if (detail::devKeysAccepted()) {
        CHECK(li.valid && contains(li.message, "[DEVELOPMENT KEY]"));
    } else {
        CHECK(!li.valid && !contains(li.message, "Licensed"));
    }
    // the production key never equals the development key
    uint8_t devPub[32];
    sign::derivePublicKey(devSeed, devPub);
    CHECK(!contains(detail::productionKeyFingerprints(), fmt::keyFingerprint(devPub)));
    CHECK(detail::productionKeyFingerprints().size() >= 16);
    // a key from an unknown signer is rejected by the public API
    TestKeyPair kp(8);
    CHECK(!verifyKey(signOrDie(basePayload(), kp), kNow).valid);
}

CEL_TEST(dev_sample_keys_file) {
    // tools/licensegen/dev-keys/sample-keys.txt is used by the host/render tests: keep it truthful.
    const std::string text = readText(std::string(CEL_LICENSE_REPO_ROOT) + "/tools/licensegen/dev-keys/sample-keys.txt");
    std::istringstream in(text);
    std::string line;
    int n = 0;
    const int64_t now = int64_t(std::time(nullptr));
    while (std::getline(in, line)) {
        if (line.rfind("CELL1-", 0) != 0) continue;
        ++n;
        const LicenseInfo li = verifyKey(line, now);
        if (!detail::devKeysAccepted()) { CHECK(!li.valid); continue; }
        if (li.licenseId == "DEV-EXPIRED") CHECK(!li.valid && contains(li.message, "expired"));
        else if (li.licenseId == "DEV-TRIAL") CHECK(li.valid && needsWatermark(li));
        else CHECK_MSG(li.valid && !needsWatermark(li), li.message);
    }
    CHECK(n == 5);
}
