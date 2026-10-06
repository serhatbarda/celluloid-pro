# DEVELOPMENT license key pair - not a secret, not for production

These files exist so tests and local builds can exercise activation end to end:

| file | what |
|------|------|
| `celluloid-dev.private.key` | Ed25519 private key (public on purpose - anyone can sign "dev" keys with it) |
| `celluloid-dev.public.key`  | matching public key; compiled into `src/license/DevPublicKey.h` |
| `sample-keys.txt`           | ready-made dev-signed keys (Pro, Studio, Indie, expired, Trial edition) |

The development public key is compiled into the plug-in **only** when the build is configured with
`-DCEL_DEV_LICENSE=ON`. Release builds do not contain it (`tests/license/build.sh` checks the
binary), so keys signed with this pair are rejected there with "signature check failed". Every
status message of a dev-signed key ends in `[DEVELOPMENT KEY]`.

Sign a dev key:

    licensegen sign --key tools/licensegen/dev-keys/celluloid-dev.private.key --licensee "Tester" --edition pro

The production signing key is something else entirely and never lives in this repository - see
`../README.md`.
