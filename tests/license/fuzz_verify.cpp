// libFuzzer harness for the license key parser / verifier (run by tests/license/build.sh when
// FUZZ_SECONDS > 0 and clang is available):
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined -DCEL_DEV_LICENSE=1 \
//       tests/license/fuzz_verify.cpp src/license/{License,KeyFormat,KeySigner}.cpp third_party/monocypher/*.c
#include "../../src/license/KeyFormat.h"
#include "../../src/license/KeySigner.h"
#include "../../src/license/LicenseInternal.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace cel::lic;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static uint8_t seed[32], pub[32];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i * 13 + 5);
        sign::derivePublicKey(seed, pub);
        init = true;
    }
    const std::string text(reinterpret_cast<const char*>(data), size);
    const detail::TrustedKey tk{pub, false};

    // 1. the full verifier on arbitrary text, with every clock edge case
    for (int64_t now : {int64_t(0), int64_t(-1), int64_t(1791504000), int64_t(1) << 41}) {
        const LicenseInfo li = detail::verifyKeyWithKeys(text, now, &tk, 1);
        if (li.message.empty()) __builtin_trap();
        if (li.valid && li.licenseId.empty()) __builtin_trap();
    }
    (void)verifyKey(text, 1791504000);

    // 2. the strict payload parser on raw bytes; whatever it accepts must re-encode identically
    fmt::Payload p;
    if (fmt::decodePayload(data, size, p, nullptr)) {
        std::vector<uint8_t> again;
        if (!fmt::encodePayload(p, again, nullptr)) __builtin_trap();
        if (again.size() != size || std::memcmp(again.data(), data, size) != 0) __builtin_trap();
        // and a key signed from it must verify
        std::string key;
        if (!sign::signLicenseKey(p, seed, key, nullptr)) __builtin_trap();
        const LicenseInfo li = detail::verifyKeyWithKeys(key, 0, &tk, 1);
        if (li.licensee != p.licensee) __builtin_trap();
    }

    // 3. base32 round trip
    std::vector<uint8_t> raw;
    if (fmt::base32Decode(text, raw)) {
        std::string up = text;
        for (char& c : up) if (c >= 'a' && c <= 'z') c = char(c - 32);
        if (fmt::base32Encode(raw.data(), raw.size()) != up) __builtin_trap();
    }
    std::string canon;
    (void)fmt::normalizeKeyText(text, canon);
    uint8_t h[32];
    (void)fmt::hexDecode(text, h, 32);
    return 0;
}
