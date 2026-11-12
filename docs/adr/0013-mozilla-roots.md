# 0013. Mozilla's root store, imported one certificate at a time

- Status: Proposed
- Date: 2026-11-12
- Supersedes: [0006](0006-switch-tls-service.md), decision 1 ("public certificate
  authorities are checked against the Switch's own trusted roots"; the app
  bundles no root store). ADR 0006's other decisions stand.

## Context

ADR 0006 dropped the bundled root store for a good reason. This libcurl's TLS
backend is the Switch's own ssl service, and it imports `CURLOPT_CAINFO` with one
call. So a bundle in one file only ever trusted its first certificate. Public
servers were left to the Switch's built-in trusted roots, and the user's own CA
went in through `CURLOPT_CAPATH`, which is imported file by file.

ADR 0006 expected that a Switch "may lack a newer public root". On the
maintainer's Switch (firmware 23.5.0), pairing with a server behind Cloudflare
failed: "the server's certificate isn't trusted", with the clock right. Cloudflare
serves Let's Encrypt's newer ECDSA chain:

```
leaf ← YE2 ← Root YE ← ISRG Root X2 ← ISRG Root X1
```

The Switch's built-in list doesn't let it through. Any server with a common public
certificate can hit this, and asking users to fetch roots into `ca.pem` is the
kind of step `CLAUDE.md`'s TLS rule ("using a bundled root CA store") was meant
to avoid.

We don't know two things about the ssl service, which only the console can tell
us:

1. How long importing about 120 certificates (118 in the pinned release) takes. The backend imports the whole
   `CURLOPT_CAPATH` folder on every new connection.
2. Whether a context has a limit on imported certificates.

## Decision

1. **Mozilla's root store is bundled again,** as ADR 0005 had it:
   - `romfs/cacert.pem`, from a pinned certifi release with its wheel's SHA-256;
   - `scripts/update-ca-bundle.py` to reproduce it;
   - the "Trusted roots match the pin" CI job to check it.
2. **It's imported one certificate per file,** like the user's CA (ADR 0006
   decision 2). At start, the app writes the bundle's certificates, followed by
   the user's CA, to `/config/dreadovania/trusted-public/`. To spare the SD
   card, it rewrites that folder only when the bundle or `ca.pem` changes,
   tracked by a SHA-256 stamp.
3. **Two steps, so ordinary connections cost nothing more:**
   - **First try:** a connection trusts the Switch's roots and the user's CA
     (`trusted/`), as now.
   - **On failure:** if the server's certificate isn't trusted, the app tries
     once more with `trusted-public/`.
   - **Remembered:** if that works, it uses `trusted-public/` for that server
     from then on, until the app restarts.

   Both steps verify fully: Mozilla's roots only add trust anchors, the same
   ones curl, Firefox and most operating systems use. Nothing skips a check.
4. **Messages:** when both steps fail, the error says Mozilla's roots were tried
   too, and still names the clock and `ca.pem`.
5. **Hardware decides the rest.** The first build's checklist includes:
   - pairing through Cloudflare without anything in `ca.pem`;
   - the time a fresh connection takes with `trusted-public/`.

   If the import is quick, a later change can drop the first step. If the
   service refuses that many certificates, this ADR is revisited.

## Consequences

- The `.nro` grows by the bundle, about 230 KB. Starting the app the first time,
  or after an update to the bundle, writes about 120 small files to the SD card.
- Mozilla's store changes over time. `scripts/update-ca-bundle.py` updates it,
  through a pull request.
- A server with a public certificate the Switch already trusts works as before,
  with no extra cost. One the Switch doesn't trust costs one failed handshake per
  app start, then works.
- `ca.pem` remains the way to trust a private CA. It's no longer needed for
  public ones.
