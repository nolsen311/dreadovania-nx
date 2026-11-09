# 0005. Trusted roots from certifi, and a small JSON reader of our own

- Status: Accepted
- Date: 2026-11-09
- Amends: [0004](0004-phase4-plan.md), decision 4 (where the roots come from)

## Context

ADR 0004 bundles Mozilla's root store "as published by the curl project",
downloaded and checked in CI. Two things came up while building pairing:

- The machine the app is developed on can't reach curl.se, so the bundle and
  its hash couldn't be produced or checked there; only GitHub's runners could.
- The app needs to read JSON (the server's answers and its own settings file).
  devkitPro ships jansson for the Switch, but the host unit tests would then
  need a second copy from the runner's package manager, at whatever version it
  has, and it couldn't be built on the development machine either.

## Decision

- **Trusted roots come from [certifi](https://github.com/certifi/python-certifi)**,
  which packages the same Mozilla root store (MPL-2.0) for Python and
  publishes every release on PyPI with SHA-256 hashes.
  - `romfs/cacert.pem` is committed, so every build uses exactly the reviewed
    file.
  - `scripts/update-ca-bundle.py` pins a certifi version and its wheel's
    SHA-256, and reproduces the file from it. CI runs it with `--check` on
    every pull request.
  - Updating the roots is a pull request that changes the pin and the file.
  - Everything else in ADR 0004 stands: certificates are always verified, the
    user may add their own CA at `/config/dreadovania/ca.pem`, and there is no
    option to skip verification.
- **JSON is read by a small reader in `core/`** (`json.hpp`): objects, arrays,
  strings with escapes and UTF-16 surrogates, numbers, booleans and null; at
  most 32 levels deep and 4 MB. It needs no exceptions, works the same on the
  Switch and in the host tests, and adds no dependency. Its tests include
  20,000 randomly corrupted inputs under AddressSanitizer and UBSan.

## Consequences

- The roots are as current as the pinned certifi release. Dependabot doesn't
  track it; refreshing it is a manual pull request, worth doing with each app
  release.
- The JSON reader is ours to maintain. It only reads answers from the user's
  own server over verified TLS, and the app's own settings file.
