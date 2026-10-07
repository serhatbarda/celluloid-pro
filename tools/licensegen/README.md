# Celluloid Pro licensing and `licensegen`

Celluloid Pro uses **offline, signed license keys**. A key is a line of text such as

    CELL1-AEBA2Y3FNRWHK3DPNFSC24DSN4QUUYLOMUQEG33MN52XE2LTOQQDY2TBNZSUAZLYMFWXA3DF...

that carries who the license is for, which edition it is, when it was issued, when it expires
(or that it never does) and how many seats it covers, plus an **Ed25519 signature** (RFC 8032).
The plug-in contains only the *public* key. It checks the signature locally, with no network
access, no activation server and no phone-home. The *private* signing key stays with the vendor
and is the only thing that can issue keys.

Without a valid key the plug-in runs as a **trial**: every feature is on except LUT export
(a .cube would carry the whole colour pipeline without the watermark), and renders carry a
small "CELLULOID PRO — TRIAL" badge in the bottom-right corner and a faint diagonal band.

- [What offline keys can and cannot do](#what-offline-keys-can-and-cannot-do)
- [Building licensegen](#building-licensegen)
- [One-time setup: the production signing key](#one-time-setup-the-production-signing-key)
- [Issuing keys](#issuing-keys)
- [Web-shop integration (keys by e-mail)](#web-shop-integration-keys-by-e-mail)
- [Activation on the customer's machine](#activation-on-the-customers-machine)
- [Development keys](#development-keys)
- [Key format reference](#key-format-reference)
- [Command reference](#command-reference)

## What offline keys can and cannot do

Please read this before you sell anything.

- **A key cannot be forged.** Without the private key nobody can create a key the plug-in
  accepts, and changing one character of a key (the name, the edition, the expiry date) breaks
  the signature. Monocypher's Ed25519 implementation is vendored in `third_party/monocypher`.
- **A key can be copied.** The same key works on any number of computers. The `seats` field is
  shown to the customer ("Studio, 10 seats"), but it is not enforced: seat limits rely on the
  customer's honesty and on the licence agreement. Real enforcement would need an online
  activation server, which Celluloid Pro does not have (yet).
- **A key cannot be revoked.** Once issued, a perpetual key stays valid. If a key leaks (for
  example on a forum), the remedy is to rotate the signing key in a later release (see
  [Key rotation](#key-rotation)) and send all honest customers new keys. Keys with an expiry
  date (subscriptions, rentals) limit the damage.
- **The binary can be patched.** Any locally checked licence can be removed by someone who edits
  the machine code. The goal is to keep honest people honest and to make the paid version the
  convenient one, not to stop determined crackers.
- **Clock tolerance.** Keys are accepted up to 48 hours before their issue date and up to 48
  hours after their expiry, to allow for time zones and slightly wrong clocks. Expiring keys are
  rejected when the computer has no usable clock. Nothing prevents a user from setting the
  clock back to keep using an expired subscription key.

The licensee name is part of every key and is shown in the plug-in's License group, which
discourages casual sharing: a leaked key carries the buyer's name.

## Building licensegen

`licensegen` is built with the rest of the project (`-DCEL_BUILD_TOOLS=ON`, the default):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target licensegen
    build/tools/licensegen/licensegen help

Without CMake (any C++17 compiler):

    cc -O2 -c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
    c++ -std=c++17 -O2 -o licensegen tools/licensegen/licensegen.cpp src/license/License.cpp \
        src/license/KeyFormat.cpp src/license/KeySigner.cpp monocypher.o monocypher-ed25519.o
    # Windows (MinGW): add -lshell32 -lole32 -lbcrypt; with MSVC, CMake handles everything

`tests/license/build.sh` builds it too and runs it end to end.

## One-time setup: the production signing key

The repository ships a **provisional** production public key (`src/license/PublicKey.h`, marked
`kProductionKeyProvisional = true`). It was generated on the development machine during the
build phase. Its private half is not in the repository and must not be relied on. Before the
first commercial release, do this once on your own computer, ideally one that is offline and
encrypted. (CI enforces this: a tag `v*` is not published while `PublicKey.h` is provisional, the
build has `CEL_DEV_LICENSE=ON` or the development public key is found in the plug-in binary —
`installers/ci/release_gate.sh`. Normal `main` builds and their test installers are not gated.)

1. Generate the keypair outside every git repository. `licensegen` refuses to write a
   production key inside one:

       licensegen keygen --name celluloid-signing
       # -> ~/.celluloid-license-keys/celluloid-signing.private.key   (0600, KEEP SECRET)
       #    ~/.celluloid-license-keys/celluloid-signing.public.key
       #    fingerprint: 3f0c...

2. **Back up the private key** in at least two safe places, for example an encrypted USB stick
   and a password manager. If you lose it you cannot issue keys any more. Every existing key
   would keep working, but new customers would need a plug-in update with a new public key.

3. Put the public key into the source tree and commit it:

       licensegen header --pub ~/.celluloid-license-keys/celluloid-signing.public.key \
                         --out src/license/PublicKey.h
       git add src/license/PublicKey.h && git commit -m "Production license public key"

   The CMake configure step prints a notice while the header is still provisional, and
   `cel::lic::detail::productionKeyIsProvisional()` reports it at run time.

4. Check that the build accepts your keys. With a build configured with
   `-DCEL_DEV_LICENSE=OFF`, run
   `licensegen verify "$(licensegen sign --key <private.key> --licensee Test)"`: the tool
   checks against the public keys compiled into it.

### Key rotation

`licensegen header` takes several `--pub` arguments, and the plug-in accepts keys signed by
any of them:

    licensegen header --pub old.public.key --pub new.public.key --out src/license/PublicKey.h

Ship a release with both keys, sign all new keys with the new private key, and remove the old
key in a later release once your customers have been sent replacement keys.

## Issuing keys

    licensegen sign --key ~/.celluloid-license-keys/celluloid-signing.private.key \
                    --licensee "Jane Colourist <jane@example.com>" \
                    --edition pro            # trial | indie | pro | studio  (default pro)
                    --id ORDER-10045         # default: random CP-XXXXXXXXXXXXXXXX
                    --seats 1                # 1..65535, informational
                    --expires never          # never | YYYY-MM-DD | +365d | @UNIX
                    --issued now             # now | YYYY-MM-DD | @UNIX
                    [--json]

- `--licensee`: 1 to 128 bytes of UTF-8 (any language). Control characters and bidirectional
  override characters are rejected, so a name cannot be made to display misleadingly.
  Leading and trailing blanks are trimmed. Use "Name <email>" or a company name. It is shown to
  the user.
- `--id`: your order or invoice number (`[A-Za-z0-9._-]`, up to 64 characters). It lets you
  find the order behind any key a customer sends you.
- `--expires 2027-06-30`: the key works through the end of 30 June 2027 (UTC). `+365d` is
  counted from `--issued`.
- `--edition trial`: a signed evaluation key. It is valid, but the watermark stays on.
- `--json` prints `{"key": ..., "edition": ..., "licensee": ..., "id": ..., "issued": ...,
  "expires": ..., "seats": ...}` for scripts.

Typical key length: 230 to 260 characters, at most about 480. Keys survive e-mail line
wrapping. When a key is pasted, whitespace and line breaks are ignored, and so are
non-breaking and zero-width spaces and letter case.

Check any key, for example one a customer says does not work:

    licensegen verify --pub ~/.celluloid-license-keys/celluloid-signing.public.key 'CELL1-...'
    licensegen verify 'CELL1-...'        # against the keys compiled into licensegen (+ dev key)
    echo 'CELL1-...' | licensegen verify --json -

Exit codes: `0` valid, `1` invalid key, `2` usage or I/O error.

## Web-shop integration (keys by e-mail)

Most shops (Shopify, WooCommerce, Paddle, Lemon Squeezy, Gumroad, FastSpring, Stripe Checkout
...) can call a URL (a *webhook*) when an order is paid. The usual setup:

```
customer pays ──► shop ──(webhook: order paid)──► your small signing service
                                                    1. verify the webhook signature
                                                    2. idempotency: order already has a key? -> resend it
                                                    3. sign: licensee = name/e-mail, id = order number,
                                                       edition/seats/expiry from the product SKU
                                                    4. store (order id, key) in your database
                                                    5. send the e-mail (or hand the key back to the shop
                                                       if it supports "license key" fulfilment)
```

The signing service can be a serverless function (AWS Lambda, Google Cloud Functions,
Cloudflare Workers, Vercel ...) or a small VPS. It can either

- run the `licensegen` binary (`licensegen sign ... --json`), or
- use the **reference Python implementation** `tools/licensegen/celluloid_license.py`. It needs
  `pip install cryptography`, and `tests/license/build.sh` checks that it produces keys
  byte-identical to the C++ code:

```python
from celluloid_license import load_private_key, issue_key

SEED = load_private_key("/run/secrets/celluloid-signing.private.key")   # or from a secret manager

def on_order_paid(order):                       # called by your webhook handler
    if order.license_key:                       # webhook retries: never issue twice
        return order.license_key
    edition, seats = {"CP-INDIE": ("indie", 1), "CP-PRO": ("pro", 1),
                      "CP-STUDIO-10": ("studio", 10)}[order.sku]
    key = issue_key(SEED, licensee=f"{order.name} <{order.email}>"[:128], edition=edition,
                    license_id=f"ORDER-{order.number}", seats=seats)   # perpetual by default
    order.license_key = key                     # persist BEFORE sending the e-mail
    send_mail(order.email, "Your Celluloid Pro license", LICENSE_MAIL.format(key=key, name=order.name))
    return key
```

The same format can be implemented in any language with a standard Ed25519 library (Node
`crypto.sign(null, msg, key)`, Go `crypto/ed25519`, libsodium `crypto_sign_detached`). See
[Key format reference](#key-format-reference).

**Protect the private key on the server.** Keep it in the platform's secret store
(environment secret, AWS Secrets Manager, GCP Secret Manager), never in the shop's theme, CMS
or a public repository. The signing endpoint must only respond to authenticated webhooks:
check the shop's HMAC signature header. Anyone who can call it can mint licenses.

**Suggested e-mail text:**

> Thank you for buying Celluloid Pro!
>
> Your license key (copy the whole line):
>
>     {key}
>
> To activate: in DaVinci Resolve, add Celluloid Pro to a node, open the **License** group in
> the Inspector, paste the key into **License Key** and click **Activate**. The trial
> watermark disappears immediately. The key is stored for your user account; copy it to every
> computer you are licensed for.
>
> Keep this e-mail. If you reinstall or change computers, just paste the key again.

Keep a record of (order id, licensee, key) so you can resend keys and answer support requests.
Refunds cannot revoke a key. Rely on your terms of sale.

## Activation on the customer's machine

In the plug-in, the **License** group shows the status (`licenseStatus`). Paste the key into
`licenseKey` and click `licenseActivate`. `licenseRemove` deletes the stored key. The plug-in
checks the key before saving it, so an invalid or expired key is reported and never stored.

The key is stored as a small text file:

| platform | per user (written by Activate) | machine-wide (studios, read-only for the plug-in) |
|----------|-------------------------------|---------------------------------------------------|
| macOS    | `~/Library/Application Support/Celluloid/license.key` | `/Library/Application Support/Celluloid/license.key` |
| Windows  | `%APPDATA%\Celluloid\license.key` | `%PROGRAMDATA%\Celluloid\license.key` |
| Linux    | `$XDG_CONFIG_HOME/celluloid/license.key` or `~/.config/celluloid/license.key` | `/etc/celluloid/license.key` |

Order of precedence: the first **valid paid** key wins (Indie, Pro, Studio), checked in this
order: the per-user file, then the environment variable `CELLULOID_LICENSE_KEY` (for render
nodes and containers), then the machine-wide file. A valid Trial-edition (evaluation) key is
used only when no source holds a paid key, so a leftover evaluation key in a user's settings
never re-watermarks a machine that is licensed machine-wide. If none is valid, the status explains the first invalid one (for
example "Saved license key: This license expired on 2027-06-30"). In key files, lines starting
with `#` are comments, so administrators can annotate machine-wide files.

**Studio deployment:** copy a Studio key to the machine-wide path, for example with your
configuration management (the file may be a symlink). Then every user on that machine is
licensed without activating. On Windows, `%PROGRAMDATA%\Celluloid` needs administrator rights
to write.

The plug-in re-reads the files at most every 1.5 s, outside the render threads' critical path
(render threads keep the previous status while one of them re-reads; only the very first status
of a session is loaded synchronously, and Activate/Deactivate reload on the UI thread), and
re-checks expiry every minute. A key copied in by hand is picked up without restarting
Resolve.

## Development keys

`tools/licensegen/dev-keys/` holds a **public, clearly marked development keypair** and
ready-made sample keys (`sample-keys.txt`). The development public key is compiled in only
when the build is configured with `-DCEL_DEV_LICENSE=ON`. Status messages then end in
`[DEVELOPMENT KEY]`. Release builds do not contain that key at all, and
`tests/license/build.sh` checks the binary for it. Never ship a `CEL_DEV_LICENSE=ON` build.

    licensegen sign --key tools/licensegen/dev-keys/celluloid-dev.private.key --licensee Tester

## Key format reference

`CELL1-` + Base32 (RFC 4648 alphabet `A-Z2-7`, no padding, canonical) of
`payload || signature`:

| bytes | field | notes |
|-------|-------|-------|
| 1 | version | `0x01` |
| 1 | edition | 0 Trial, 1 Indie, 2 Pro, 3 Studio |
| 1+n | product | length-prefixed, `celluloid-pro` |
| 1+n | licensee | length 1..128, UTF-8, no control or bidi characters, not blank, no leading/trailing space |
| 1+n | license id | length 1..64, `[A-Za-z0-9._-]` |
| 8 | issued | int64 little-endian, Unix seconds UTC, 1..2^40 |
| 8 | expires | int64 LE, 0 = perpetual, otherwise > issued (exclusive end) |
| 2 | seats | uint16 LE, 1..65535 |
| 64 | signature | Ed25519 (RFC 8032) over `"CelluloidPro/license-key/v1\0"` + payload |

The verifier checks, in this order: prefix, Base32 syntax, size, version byte, signature,
strict payload parse (exact length, every field), product, then dates against the clock with
the ±48 h tolerance. Implementation: `src/license/KeyFormat.{h,cpp}` (format),
`src/license/License.cpp` (verification), `src/license/Persist.cpp` (storage and cache);
tests: `tests/license/`.

## Command reference

```
licensegen keygen [--out-dir DIR] [--name NAME] [--force]
licensegen keygen --dev --out-dir DIR [--name NAME] [--force]
licensegen header --pub FILE [--pub FILE ...] --out PublicKey.h [--provisional]
licensegen header --dev --pub FILE --out DevPublicKey.h
licensegen pubkey --key PRIVATE.key
licensegen fingerprint --pub FILE
licensegen sign --key PRIVATE.key --licensee TEXT [--edition E] [--id ID] [--issued WHEN]
                [--expires WHEN] [--seats N] [--product P] [--json]
licensegen verify [--pub FILE ...] [--now WHEN] [--json] KEY|-
```

Key files are text. The private key file holds `CELPRIV1:<64 hex digits>` (the 32-byte
RFC 8032 seed) and the public key file holds `CELPUB1:<64 hex>`; lines starting with `#` are
comments. On POSIX systems private key files are created with mode 0600, and `licensegen`
warns when a private key file is readable by other users.
