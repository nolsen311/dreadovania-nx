# 0016. The Switch's own roots again: no bundled root store

- Status: Proposed
- Date: 2026-11-12
- Supersedes: [0013](0013-mozilla-roots.md) and [0014](0014-root-batches.md) in full.
  Restores [0006](0006-switch-tls-service.md) decision 1. [0015](0015-rsa-cipher-and-own-curl.md)
  stands.

## Context

ADR 0013 bundled Mozilla's root store because pairing through Cloudflare failed
with "the server's certificate isn't trusted", and a missing root looked like the
cause. ADR 0014 split the roots into batches, because the Switch's TLS service
won't import more than about 70 into one connection.

On the maintainer's Switch (firmware 23.5.0), with the verify codes from
nolsen311/dreadovania-nx#41, every attempt failed with the same verify error,
`0x2867B` (2123-0323):

- with ECDSA and RSA certificates (ADR 0015's retry);
- with Let's Encrypt's certificate, and then Google Trust Services';
- with every batch of Mozilla's roots;
- with the issuing intermediate, and then Root YR itself, in `ca.pem`.

`www.google.com` and `www.cloudflare.com` worked from the same Switch. The cause
turned out to be Cloudflare asking for a client certificate during the handshake
(mTLS on the hostname), before the Switch had one; nolsen311/dreadovania-nx#43
names that error. With the hostname off the mTLS list, or through a pairing
hostname without it, the Switch paired.

Adding roots never turned a failure into a success. A success doesn't show which
attempt worked, so it isn't proven that the Switch's own roots alone were enough
for those servers. They're the most common public chains (Google Trust Services
through GlobalSign Root CA, Let's Encrypt through ISRG Root X1), so it's expected,
and the first build without the bundle checks it (Consequences).

Meanwhile the bundle costs every user:

- about 230 KB in the `.nro`;
- about 120 files written to the SD card when the bundle or `ca.pem` changes;
- up to 8 more handshakes before a real error shows, with a message too long
  for the screen;
- the batch and stamp code, the update script, and its CI check, to maintain.

## Decision

1. **No bundled root store.** Public certificates are checked against the
   Switch's own trusted roots, and a private CA against `ca.pem`, as in ADR 0006.
   `romfs/cacert.pem`, `scripts/update-ca-bundle.py` and the "Trusted roots match
   the pin" CI job are removed. With nothing left in RomFS, the `.nro` has none.
2. **The attempts are now:** the Switch's roots with `ca.pem`, then once more
   asking for an RSA certificate (ADR 0015, firmware 17.0.0+). What worked is
   remembered per server until the app restarts.
3. **Old files are removed.** At start, the app deletes the `.pem` files that
   0.8.0 and 0.8.1 wrote to `/config/dreadovania/trusted-public*`, then those
   folders if they're empty, and the stamp. It lists a folder before deleting
   anything in it, and deletes nothing else.
4. **`CLAUDE.md`'s TLS rule** said "using a bundled root CA store". It now says
   certificates are verified against the Switch's own roots plus `ca.pem`. Still
   never an option to ignore certificate errors. That's the maintainer's rule, so
   the change is theirs to accept in this pull request.

## Consequences

- **Checked on hardware before release:** with this build, pairing through the
  Cloudflare pairing hostname and loading seeds through the main one, on the
  guest network. If either fails as "not trusted", this pull request isn't
  merged as it is.

- A server whose public root the Switch doesn't have can't be reached until it's
  added to `ca.pem`, or until a system update adds it. None has turned up yet.
  If one does, this ADR is revisited: bring back only that root, not all of
  Mozilla's.
- Users who installed 0.8.0 or 0.8.1 get the SD card's files back on the first
  start.
- ADRs 0013 and 0014, and the code in git history, show how to bring the bundle
  back if it's needed.
