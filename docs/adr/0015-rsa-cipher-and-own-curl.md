# 0015. Ask for an RSA certificate when an ECDSA one can't be checked, with our own libcurl build

- Status: Proposed
- Date: 2026-11-12

## Context

Pairing with a server behind Cloudflare still failed with 0.8.1, with the
message "the server's certificate isn't trusted, by the Switch or by Mozilla's
public roots". ADR 0014's batches had all imported, so the roots weren't the
problem. On the maintainer's Switch (firmware 23.5.0), tests showed:

- **Trusting the issuing intermediate (YE2) directly didn't help either.** Only
  one signature was left to check, YE2's on the server's certificate.
- **Cloudflare's chain is ECDSA:** a P-256 key, signed with
  `ecdsa-with-SHA384` by YE2, which has a P-384 key. The Switch's TLS service
  evidently can't check that signature.
- **The maintainer's own server works:** RSA-2048 with `sha256WithRSAEncryption`.
- **Cloudflare also has an RSA chain for the hostname:** offered only RSA
  (`-cipher ECDHE-RSA-AES128-GCM-SHA256 -sigalgs RSA+SHA256`), it serves an
  RSA-2048 certificate: `leaf ← YR2 ← Root YR ← ISRG Root X1`. Cloudflare sends
  the RSA certificate only to clients that don't offer ECDSA.

Since 17.0.0, the TLS service has a connection option,
`SslPrivateOptionType_SetCipher`, that offers exactly one cipher suite. libnx
exposes it as `sslConnectionSetPrivateOption`. devkitPro's `switch-curl` (7.69.1,
`vtls/libnx.c` from their `switch-curl.patch`) doesn't use it. It ignores
`CURLOPT_SSL_CIPHER_LIST`, and its only hook (`CURLOPT_SSL_CTX_FUNCTION`) runs
before the connection exists.

## Decision

1. **We build libcurl ourselves,** as devkitPro's `PKGBUILD` does, plus
   `patches/curl/dreadovania-libnx.patch`:
   - **Build steps (`scripts/build-curl.sh`):** the same curl 7.69.1 release, the
     same devkitPro patch and the same configure options.
   - **Pinned sources:** both downloads are checked against the SHA-256 that
     devkitPro's `PKGBUILD` pins. The tarball comes from curl's GitHub release.
   - **Build tools:** the script needs autoconf, automake and libtool (the
     devkitPro patch changes `configure.ac`). It installs them with apt when it
     runs as root in the pinned container, as in CI.
   - **When it runs:** `make` builds libcurl into `build-curl/prefix/` once, and
     links it before the portlibs copy.
2. **Our patch** changes `lib/vtls/libnx.c` only:
   - **Cipher choice:** `CURLOPT_SSL_CIPHER_LIST` takes one IANA cipher-suite
     number (for example `0xC02F`). It's passed to `SetCipher` on 17.0.0 and
     later; on older firmware, or for anything else, it fails clearly
     (`CURLE_SSL_CIPHER`).
   - **A leak:** `load_capath` now closes the CA folder when it returns early.
     It leaked a folder handle per failed import, which mattered with ADR 0014's
     retries.
3. **An RSA step for each CA folder:** after the first try, the app tries each
   CA folder twice:
   - first plainly, as in ADR 0014;
   - then offering only `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256` (`0xC02F`), so
     a server with both certificate types sends its RSA one.

   The first RSA try uses the user's CA folder; the Switch's own roots are
   trusted in every try. The step that works is remembered per server until the
   app restarts. The RSA steps only run on 17.0.0 and later. Every step still
   verifies the certificate fully, with TLS 1.2 only (the backend's maximum
   unless asked otherwise).

## Consequences

- **Maintenance:** we now maintain a patched libcurl. When devkitPro updates
  `switch-curl`, update the version, URLs and hashes in `scripts/build-curl.sh`,
  and check that our patch still applies.
- **Build cost:** the first `make` in a fresh checkout takes a few minutes
  longer, while libcurl builds. `make clean` keeps `build-curl/`.
- **Connection cost:** a server like this costs up to three failed handshakes at
  the first connection after each app start, then works straight away.
- **To confirm on hardware:** that `SetCipher` takes the IANA number. If it
  doesn't, the RSA steps fail with a TLS error rather than a trust error, and
  that's the place to look.
