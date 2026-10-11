// Reafilm Pro - license key issuing (signing side). See KeySigner.h.
#include "KeySigner.h"
#include "../../third_party/monocypher/monocypher.h"
#include "../../third_party/monocypher/monocypher-ed25519.h"

#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <bcrypt.h>
  #if defined(_MSC_VER)
    #pragma comment(lib, "bcrypt.lib")
  #endif
#elif defined(__APPLE__)
  #include <sys/random.h>   // getentropy (macOS 10.12+)
#elif defined(__linux__)
  #include <sys/random.h>   // getrandom (glibc 2.25+)
  #include <cerrno>
#endif

namespace cel { namespace lic { namespace sign {

void derivePublicKey(const uint8_t seed[fmt::kSeedSize], uint8_t pub[fmt::kPublicKeySize]) {
    uint8_t s[32], sk[64];
    std::memcpy(s, seed, 32);
    crypto_ed25519_key_pair(sk, pub, s);   // wipes s
    crypto_wipe(sk, sizeof(sk));
}

bool signLicenseKey(const fmt::Payload& payload, const uint8_t seed[fmt::kSeedSize],
                    std::string& keyOut, std::string* err) {
    keyOut.clear();
    std::vector<uint8_t> blob;
    if (!fmt::encodePayload(payload, blob, err)) return false;
    const std::vector<uint8_t> msg = fmt::signingMessage(blob.data(), blob.size());

    uint8_t s[32], sk[64], pub[32], sig[64];
    std::memcpy(s, seed, 32);
    crypto_ed25519_key_pair(sk, pub, s);
    crypto_ed25519_sign(sig, sk, msg.data(), msg.size());
    crypto_wipe(sk, sizeof(sk));
    if (crypto_ed25519_check(sig, pub, msg.data(), msg.size()) != 0) {
        if (err) *err = "internal error: signature self-check failed";
        return false;
    }
    blob.insert(blob.end(), sig, sig + 64);
    keyOut = std::string(fmt::kKeyPrefix) + fmt::base32Encode(blob.data(), blob.size());
    return true;
}

bool osRandomBytes(uint8_t* out, size_t n) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    while (n > 0) {
        const size_t c = n > 256 ? 256 : n;
        if (getentropy(out, c) != 0) return false;
        out += c; n -= c;
    }
    return true;
#else
  #if defined(__linux__)
    size_t got = 0;
    while (got < n) {
        const ssize_t r = getrandom(out + got, n - got, 0);
        if (r < 0) { if (errno == EINTR) continue; break; }
        got += size_t(r);
    }
    if (got == n) return true;
  #endif
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    const size_t r = std::fread(out, 1, n, f);
    std::fclose(f);
    return r == n;
#endif
}

bool randomLicenseId(std::string& id) {
    uint8_t rnd[10];
    if (!osRandomBytes(rnd, sizeof(rnd))) return false;
    id = "CP-" + fmt::base32Encode(rnd, sizeof(rnd));   // 10 bytes -> exactly 16 chars
    return true;
}

}}} // namespace cel::lic::sign
