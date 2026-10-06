// Celluloid Pro - license key verification (pure, no I/O). See KeyFormat.h for the format.
//
// Order of checks: syntax -> base32 -> size -> format version -> Ed25519 signature (over the
// domain-separated payload, before the payload is interpreted) -> strict payload parse ->
// product -> dates.  Everything verified here is public data (the key and the public keys), so
// there are no secrets whose timing could leak; Monocypher's verification is constant-time anyway.
#include "License.h"
#include "KeyFormat.h"
#include "LicenseInternal.h"
#include "PublicKey.h"
#if defined(CEL_DEV_LICENSE) && CEL_DEV_LICENSE
  #include "DevPublicKey.h"
#endif
#include "../../third_party/monocypher/monocypher-ed25519.h"

#include <vector>

namespace cel { namespace lic {

namespace {

LicenseInfo invalid(const std::string& msg) {
    LicenseInfo li;
    li.valid = false;
    li.edition = Edition::Trial;
    li.message = msg;
    return li;
}

std::vector<detail::TrustedKey> compiledInKeys() {
    std::vector<detail::TrustedKey> k;
    for (int i = 0; i < keys::kProductionKeyCount; ++i) k.push_back({keys::kProductionKeys[i], false});
#if defined(CEL_DEV_LICENSE) && CEL_DEV_LICENSE
    k.push_back({keys::kDevKey, true});
#endif
    return k;
}

} // namespace

namespace detail {

std::string canonicalKey(const std::string& key) {
    std::string c;
    try {
        if (!fmt::normalizeKeyText(key, c)) return std::string();
        std::vector<uint8_t> raw;
        if (!fmt::base32Decode(c.substr(std::char_traits<char>::length(fmt::kKeyPrefix)), raw)) return std::string();
    } catch (...) {
        return std::string();
    }
    return c;
}

LicenseInfo verifyKeyWithKeys(const std::string& key, int64_t now, const TrustedKey* keys, size_t count) {
    try {
        std::string canon;
        bool blank = true;
        for (char ch : key)
            if (!(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) { blank = false; break; }
        if (blank) return invalid("No license key entered.");
        if (!fmt::normalizeKeyText(key, canon))
            return invalid("Not a Celluloid Pro license key (it must start with CELL1-).");

        std::vector<uint8_t> raw;
        const size_t plen = std::char_traits<char>::length(fmt::kKeyPrefix);
        if (!fmt::base32Decode(canon.substr(plen), raw) ||
            raw.size() < fmt::kMinPayloadBytes + fmt::kSignatureSize ||
            raw.size() > fmt::kMaxPayloadBytes + fmt::kSignatureSize)
            return invalid("The license key is damaged or incomplete - copy the whole key again.");

        const size_t payloadSize = raw.size() - fmt::kSignatureSize;
        const uint8_t* payload = raw.data();
        const uint8_t* sig = raw.data() + payloadSize;
        if (payload[0] != fmt::kPayloadVersion)
            return invalid("This license key was issued for a newer version of Celluloid Pro - please update the plugin.");

        const std::vector<uint8_t> msg = fmt::signingMessage(payload, payloadSize);
        int matched = -1;
        for (size_t i = 0; i < count; ++i) {
            if (keys[i].pub && crypto_ed25519_check(sig, keys[i].pub, msg.data(), msg.size()) == 0) {
                matched = int(i);
                break;
            }
        }
        if (matched < 0)
            return invalid("The license key is not valid (signature check failed).");

        fmt::Payload p;
        std::string perr;
        if (!fmt::decodePayload(payload, payloadSize, p, &perr))
            return invalid("The license key format is not supported (" + perr + ").");

        LicenseInfo li;
        li.edition    = p.edition;
        li.licensee   = p.licensee;
        li.licenseId  = p.id;
        li.issuedUnix = p.issued;
        li.expiresUnix = p.expires;
        li.seats      = int(p.seats);

        const bool dev = keys[matched].dev;
        const std::string devTag = dev ? " [DEVELOPMENT KEY]" : "";

        if (p.product != fmt::kProduct) {
            li.valid = false; li.edition = Edition::Trial;
            li.message = "This license key is for a different product (" + p.product + ").";
            return li;
        }
        if (now > 0 && p.issued > now + fmt::kClockSkewSeconds) {
            li.valid = false; li.edition = Edition::Trial;
            li.message = "This license key is not valid yet (issued " + fmt::formatDateUtc(p.issued) +
                         ") - check the computer's date and time.";
            return li;
        }
        if (p.expires != 0) {
            const std::string until = fmt::formatDateUtc(p.expires - 1);
            if (now <= 0) {
                li.valid = false; li.edition = Edition::Trial;
                li.message = "Cannot check the expiry date of this license: the system clock is unavailable.";
                return li;
            }
            if (now > p.expires + fmt::kClockSkewSeconds) {
                li.valid = false; li.edition = Edition::Trial;
                li.message = "This license expired on " + until + " (" + p.licensee + ").";
                return li;
            }
        }

        li.valid = true;
        std::string m = "Licensed to " + p.licensee + " - " + fmt::editionName(p.edition);
        if (p.edition == Edition::Trial) m += " (evaluation key, watermark stays on)";
        if (p.seats > 1) m += ", " + std::to_string(p.seats) + " seats";
        if (p.expires != 0) m += ", valid until " + fmt::formatDateUtc(p.expires - 1);
        m += devTag;
        li.message = m;
        return li;
    } catch (...) {
        return invalid("The license key could not be checked (out of memory).");
    }
}

bool devKeysAccepted() {
#if defined(CEL_DEV_LICENSE) && CEL_DEV_LICENSE
    return true;
#else
    return false;
#endif
}

std::string productionKeyFingerprints() {
    std::string s;
    try {
        for (int i = 0; i < keys::kProductionKeyCount; ++i) {
            if (!s.empty()) s += ", ";
            s += fmt::keyFingerprint(keys::kProductionKeys[i]);
        }
    } catch (...) {}
    return s;
}

bool productionKeyIsProvisional() { return keys::kProductionKeyProvisional; }

} // namespace detail

LicenseInfo verifyKey(const std::string& key, int64_t nowUnix) {
    try {
        const std::vector<detail::TrustedKey> k = compiledInKeys();
        return detail::verifyKeyWithKeys(key, nowUnix, k.data(), k.size());
    } catch (...) {
        return invalid("The license key could not be checked (out of memory).");
    }
}

}} // namespace cel::lic
