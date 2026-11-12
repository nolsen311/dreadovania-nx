# 0006. Certificates are checked by the Switch's TLS service

- Status: Accepted; decision 1 (no bundled root store) superseded by [0013](0013-mozilla-roots.md), restored by [0016](0016-switch-roots-only.md)
- Date: 2026-11-10
- Supersedes: the root-store parts of [0004](0004-phase4-plan.md) (decision 4) and
  [0005](0005-trusted-roots-and-json.md) (trusted roots from certifi)

## Context

ADRs 0004 and 0005 assumed libcurl used mbedTLS and bundled Mozilla's root store
(118 roots) in the app. On the maintainer's Switch, pairing with a server whose
certificate comes from their own CA failed, although the CA was in
`/config/dreadovania/ca.pem`, the certificates were correct (`openssl verify`
passes) and the clock was right.

What the devkitPro image actually contains:

- Its `switch-curl` 7.69.1 is built `--without-mbedtls --with-libnx`
  (devkitPro `pacman-packages`, `switch/curl/PKGBUILD`): TLS is done by the
  Switch's own **ssl system service**, through libnx. mbedTLS is not used.
- That backend imports `CURLOPT_CAINFO` with a single
  `sslContextImportServerPki` call. Given a file with many certificates, only the
  first is trusted, without an error. The app's combined file (118 roots, then
  the user's CA) therefore trusted one Mozilla root and not the user's CA.
- `CURLOPT_CAPATH` imports each file in the folder separately.
- The service always verifies against the Switch's built-in trusted roots, which
  firmware updates maintain; imported certificates are added to them.

## Decision

- **Public certificate authorities are checked against the Switch's own trusted
  roots.** The app no longer bundles a root store: with this backend it could not
  work as intended, and the system's store is what the TLS service trusts.
- **The user's own CA:** the app splits `/config/dreadovania/ca.pem` into one
  certificate per file under `/config/dreadovania/trusted/` and passes that
  folder as `CURLOPT_CAPATH`, so every certificate in it is trusted. Several
  certificates (a root and intermediates) may be in the one file.
- **Verification is always on**, as before: `CURLOPT_SSL_VERIFYPEER` and
  `VERIFYHOST` map to the service's peer-CA, host-name and date checks. There is
  no option to skip them.
- `romfs/cacert.pem`, `scripts/update-ca-bundle.py` and its CI check are removed,
  and the app no longer links mbedTLS.

## Consequences

- A Switch on old firmware may lack a newer public root. If a public
  certificate isn't trusted, putting that authority's root in `ca.pem` works
  around it.
- The app's TLS behaviour is now the console's: TLS versions and ciphers depend
  on the firmware, not on this app.
- The JSON decision in ADR 0005 is unaffected.
