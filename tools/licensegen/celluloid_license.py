#!/usr/bin/env python3
"""Reference implementation of the Celluloid Pro license key format (issue + verify).

For web-shop back ends that prefer to sign keys in Python instead of calling the `licensegen`
binary. Byte-for-byte compatible with src/license/KeyFormat.{h,cpp}; tests/license/build.sh
cross-checks both directions.

Requires PyCA cryptography (pip install cryptography) for Ed25519 (RFC 8032).

    python3 celluloid_license.py sign --key PRIVATE.key --licensee "Jane <jane@example.com>" \
            --edition pro [--id ORDER-1234] [--expires 2027-12-31] [--seats 1] [--json]
    python3 celluloid_license.py verify --pub PUBLIC.key KEY

As a library:

    from celluloid_license import load_private_key, issue_key
    seed = load_private_key("/secure/celluloid-signing.private.key")
    key = issue_key(seed, licensee="Jane Doe <jane@example.com>", edition="pro", license_id="ORDER-1234")
"""
import argparse
import base64
import calendar
import json
import os
import re
import struct
import sys
import time
import unicodedata

KEY_PREFIX = "CELL1-"
PRODUCT = "celluloid-pro"
PAYLOAD_VERSION = 1
SIGN_DOMAIN = b"CelluloidPro/license-key/v1\x00"   # NUL is part of the signed message
EDITIONS = {"trial": 0, "indie": 1, "pro": 2, "studio": 3}
EDITION_NAMES = {v: k.capitalize() for k, v in EDITIONS.items()}
MAX_TS = 1 << 40
CLOCK_SKEW = 48 * 3600

_ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
_PRODUCT_RE = re.compile(r"^[a-z0-9-]{1,32}$")


def _forbidden(cp):
    return (cp < 0x20 or cp == 0x7F or 0x80 <= cp <= 0x9F or cp in (0x2028, 0x2029, 0x200E, 0x200F, 0x061C, 0xFEFF)
            or 0x202A <= cp <= 0x202E or 0x2066 <= cp <= 0x2069 or 0xFFF9 <= cp <= 0xFFFB
            or (cp >= 0xFFFE and (cp & 0xFFFE) == 0xFFFE))


def valid_licensee(s):
    b = s.encode("utf-8")
    if not 1 <= len(b) <= 128 or s[0] == " " or s[-1] == " ":
        return False
    if any(_forbidden(ord(ch)) or 0xD800 <= ord(ch) <= 0xDFFF for ch in s):
        return False
    return any(not (ch in "  　" or 0x2000 <= ord(ch) <= 0x200D) for ch in s)


def _b32encode(data):
    return base64.b32encode(data).decode("ascii").rstrip("=")


def _b32decode(text):
    if len(text) % 8 in (1, 3, 6) or not re.fullmatch(r"[A-Z2-7]*", text):
        raise ValueError("bad base32")
    raw = base64.b32decode(text + "=" * (-len(text) % 8))
    if _b32encode(raw) != text:            # canonical (unused bits zero)
        raise ValueError("non-canonical base32")
    return raw


def encode_payload(edition, licensee, license_id, issued, expires=0, seats=1, product=PRODUCT):
    if edition not in EDITION_NAMES:
        raise ValueError("edition must be 0..3")
    if not _PRODUCT_RE.match(product):
        raise ValueError("bad product")
    if not valid_licensee(licensee):
        raise ValueError("licensee must be 1-128 bytes of printable UTF-8 without leading/trailing spaces")
    if not _ID_RE.match(license_id):
        raise ValueError("license id must be 1-64 chars [A-Za-z0-9._-]")
    if not 0 < issued < MAX_TS:
        raise ValueError("issued out of range")
    if expires != 0 and not issued < expires < MAX_TS:
        raise ValueError("expires must be 0 or later than issued")
    if not 1 <= seats <= 65535:
        raise ValueError("seats must be 1..65535")
    out = bytearray([PAYLOAD_VERSION, edition])
    for field in (product.encode(), licensee.encode("utf-8"), license_id.encode()):
        out.append(len(field))
        out += field
    out += struct.pack("<qqH", issued, expires, seats)
    return bytes(out)


def decode_payload(p):
    if not 26 <= len(p) <= 246 or p[0] != PAYLOAD_VERSION or p[1] > 3:
        raise ValueError("bad payload header")
    pos, fields = 2, []
    for limit in (32, 128, 64):
        n = p[pos]
        if not 1 <= n <= limit or pos + 1 + n > len(p):
            raise ValueError("bad field length")
        fields.append(p[pos + 1:pos + 1 + n].decode("utf-8"))
        pos += 1 + n
    if pos + 18 != len(p):
        raise ValueError("bad payload size")
    issued, expires, seats = struct.unpack_from("<qqH", p, pos)
    product, licensee, license_id = fields
    if (not _PRODUCT_RE.match(product) or not valid_licensee(licensee) or not _ID_RE.match(license_id)
            or not 0 < issued < MAX_TS or (expires != 0 and not issued < expires < MAX_TS) or seats < 1):
        raise ValueError("invalid payload field")
    return dict(edition=EDITION_NAMES[p[1]], product=product, licensee=licensee, id=license_id,
                issued=issued, expires=expires, seats=seats)


def _ed25519():
    try:
        from cryptography.hazmat.primitives.asymmetric import ed25519
        return ed25519
    except ImportError:
        sys.exit("celluloid_license.py needs PyCA cryptography: pip install cryptography")


def _read_tagged(path, tag):
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and line.startswith(tag):
                return bytes.fromhex(line[len(tag):].strip())
    raise ValueError("%s: no %s line" % (path, tag))


def load_private_key(path):
    seed = _read_tagged(path, "CELPRIV1:")
    if len(seed) != 32:
        raise ValueError("bad private key")
    return seed


def load_public_key(path):
    pub = _read_tagged(path, "CELPUB1:")
    if len(pub) != 32:
        raise ValueError("bad public key")
    return pub


def random_license_id():
    return "CP-" + _b32encode(os.urandom(10))


def issue_key(seed, licensee, edition="pro", license_id=None, issued=None, expires=0, seats=1, product=PRODUCT):
    """Returns the key text. `expires`: 0 = perpetual, else Unix seconds (exclusive end)."""
    ed = _ed25519()
    if isinstance(edition, str):
        edition = EDITIONS[edition.lower()]
    payload = encode_payload(edition, licensee, license_id or random_license_id(),
                             int(time.time()) if issued is None else int(issued), int(expires), int(seats), product)
    sig = ed.Ed25519PrivateKey.from_private_bytes(seed).sign(SIGN_DOMAIN + payload)
    return KEY_PREFIX + _b32encode(payload + sig)


def normalize(text):
    drop = {" ", " ", "​", "‌", "‍", "⁠", "﻿"}
    compact = "".join(ch for ch in text if not ch.isspace() and ch not in drop).upper()
    if len(text) > 4096 or not compact.startswith(KEY_PREFIX) or len(compact) == len(KEY_PREFIX):
        raise ValueError("not a CELL1- key")
    return compact


def verify_key(text, public_keys, now=None):
    """Returns the decoded fields; raises ValueError if the key is not valid now."""
    ed = _ed25519()
    from cryptography.exceptions import InvalidSignature
    raw = _b32decode(normalize(text)[len(KEY_PREFIX):])
    if not 90 <= len(raw) <= 310:
        raise ValueError("damaged key")
    payload, sig = raw[:-64], raw[-64:]
    for pub in public_keys:
        try:
            ed.Ed25519PublicKey.from_public_bytes(pub).verify(sig, SIGN_DOMAIN + payload)
            break
        except InvalidSignature:
            continue
    else:
        raise ValueError("signature check failed")
    info = decode_payload(payload)
    now = int(time.time()) if now is None else now
    if info["product"] != PRODUCT:
        raise ValueError("different product")
    if info["issued"] > now + CLOCK_SKEW:
        raise ValueError("not valid yet")
    if info["expires"] and now > info["expires"] + CLOCK_SKEW:
        raise ValueError("expired")
    return info


def _parse_day(s):
    return calendar.timegm(time.strptime(s, "%Y-%m-%d"))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True)
    s.add_argument("--licensee", required=True)
    s.add_argument("--edition", default="pro", choices=sorted(EDITIONS))
    s.add_argument("--id")
    s.add_argument("--issued", help="YYYY-MM-DD or @UNIX (default: now)")
    s.add_argument("--expires", default="never", help="never | YYYY-MM-DD (through end of day UTC) | @UNIX")
    s.add_argument("--seats", type=int, default=1)
    s.add_argument("--json", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("--pub", action="append", required=True)
    v.add_argument("--now", type=int)
    v.add_argument("key")
    a = ap.parse_args(argv)
    if a.cmd == "sign":
        issued = None
        if a.issued:
            issued = int(a.issued[1:]) if a.issued.startswith("@") else _parse_day(a.issued)
        expires = 0
        if a.expires not in ("never", "0"):
            expires = int(a.expires[1:]) if a.expires.startswith("@") else _parse_day(a.expires) + 86400
        lic_id = a.id or random_license_id()
        key = issue_key(load_private_key(a.key), unicodedata.normalize("NFC", a.licensee.strip()), a.edition,
                        lic_id, issued, expires, a.seats)
        if a.json:
            print(json.dumps(dict(key=key, licensee=a.licensee.strip(), edition=a.edition, id=lic_id), ensure_ascii=False))
        else:
            print(key)
        return 0
    try:
        info = verify_key(sys.stdin.read() if a.key == "-" else a.key, [load_public_key(p) for p in a.pub], a.now)
    except ValueError as e:
        print("invalid: %s" % e)
        return 1
    print(json.dumps(info, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
