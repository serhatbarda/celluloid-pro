// Reafilm Pro - license key issuing (signing side). Linked into tools/licensegen and the tests
// only (library target `cellic_sign`); the plugin itself never signs anything.
#pragma once
#include "KeyFormat.h"
#include <cstdint>
#include <string>

namespace cel { namespace lic { namespace sign {

// Derives the Ed25519 public key from a 32-byte private seed.
void derivePublicKey(const uint8_t seed[fmt::kSeedSize], uint8_t pub[fmt::kPublicKeySize]);

// Builds and signs a key. Validates every field; self-verifies the signature before returning.
bool signLicenseKey(const fmt::Payload& payload, const uint8_t seed[fmt::kSeedSize],
                    std::string& keyOut, std::string* err);

// Cryptographically secure random bytes from the operating system. False if unavailable.
bool osRandomBytes(uint8_t* out, size_t n);

// New random license id, e.g. "CP-7KQ2M4ZD9XAB3FHT" (16 random base32 chars = 80 bits).
bool randomLicenseId(std::string& id);

}}} // namespace cel::lic::sign
