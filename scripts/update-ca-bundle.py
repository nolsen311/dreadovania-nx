#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The app's trusted root certificates: Mozilla's root store, from certifi (ADR 0005).

The bundle is committed as romfs/cacert.pem. This script reproduces it from a
pinned certifi release on PyPI, checking the wheel's SHA-256 first.

    scripts/update-ca-bundle.py --check   # CI: the committed file matches the pin
    scripts/update-ca-bundle.py           # rewrite romfs/cacert.pem from the pin

To update: set CERTIFI_VERSION and WHEEL_SHA256 to a newer release (the hash is
on its PyPI page, "Download files"), run the script, and open a pull request.
"""

import argparse
import hashlib
import io
import json
import sys
import urllib.request
import zipfile
from pathlib import Path

CERTIFI_VERSION = "2026.10.23"
WHEEL_SHA256 = "e9b54de76e530bb6cf0ccd036787103acb58fd42ba930ea8bf948ddbaa2f61d1"
BUNDLE = Path(__file__).resolve().parent.parent / "romfs" / "cacert.pem"


def wheel_url() -> str:
    url = f"https://pypi.org/pypi/certifi/{CERTIFI_VERSION}/json"
    with urllib.request.urlopen(url, timeout=30) as response:  # noqa: S310 (fixed https URL)
        release = json.load(response)
    for file in release["urls"]:
        if file["packagetype"] == "bdist_wheel" and file["digests"]["sha256"] == WHEEL_SHA256:
            return file["url"]
    sys.exit(f"certifi {CERTIFI_VERSION} has no wheel with the pinned SHA-256")


def pinned_bundle() -> bytes:
    with urllib.request.urlopen(wheel_url(), timeout=60) as response:  # noqa: S310
        wheel = response.read()
    actual = hashlib.sha256(wheel).hexdigest()
    if actual != WHEEL_SHA256:
        sys.exit(f"the certifi wheel's SHA-256 is {actual}, not the pinned {WHEEL_SHA256}")
    return zipfile.ZipFile(io.BytesIO(wheel)).read("certifi/cacert.pem")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="compare, don't write")
    args = parser.parse_args()
    bundle = pinned_bundle()
    count = bundle.count(b"-----BEGIN CERTIFICATE-----")
    if args.check:
        if not BUNDLE.exists() or BUNDLE.read_bytes() != bundle:
            print(f"{BUNDLE} doesn't match certifi {CERTIFI_VERSION}: run this script")
            return 1
        print(f"{BUNDLE.name} matches certifi {CERTIFI_VERSION} ({count} roots)")
        return 0
    BUNDLE.parent.mkdir(exist_ok=True)
    BUNDLE.write_bytes(bundle)
    print(f"wrote {BUNDLE} from certifi {CERTIFI_VERSION} ({count} roots)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
