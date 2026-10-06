# Vendored: Monocypher 4.0.3

- Upstream: https://monocypher.org/ (source: https://github.com/LoupVaillant/Monocypher, tag `4.0.3`)
- Release tarball: `https://monocypher.org/download/monocypher-4.0.3.tar.gz`
- SHA-512 of the tarball (matches the published `monocypher-4.0.3.tar.gz.sha512`):
  `40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965`
- Licence: dual 2-clause BSD / CC-0, see `LICENCE.md` (choose either). Authors: `AUTHORS.md`.

## Files (copied byte-for-byte, unmodified)

| here                    | upstream path                         | why                                   |
|-------------------------|---------------------------------------|---------------------------------------|
| `monocypher.h/.c`       | `src/monocypher.h/.c`                 | field arithmetic, `crypto_wipe`, EdDSA core |
| `monocypher-ed25519.h/.c` | `src/optional/monocypher-ed25519.h/.c` | SHA-512 + RFC 8032 Ed25519 sign/check |

Only the RFC 8032 (SHA-512) Ed25519 functions `crypto_ed25519_key_pair`, `crypto_ed25519_sign`
and `crypto_ed25519_check` are used (plus `crypto_wipe`). The BLAKE2b-based `crypto_eddsa_*`
functions of the main file are *not* used for license keys, so keys can be produced by any
standard Ed25519 implementation (libsodium, PyCA `cryptography`, Node `crypto`, Go `crypto/ed25519`).

Both directories are flat on purpose: `monocypher-ed25519.c` includes `"monocypher-ed25519.h"`,
which includes `"monocypher.h"`, so no extra include path is required.

To update: download the new release from monocypher.org, verify its published SHA-512, replace
the four source files + `LICENCE.md` + `AUTHORS.md`, update this file, and run
`tests/license/build.sh` (it includes the RFC 8032 test vectors).
