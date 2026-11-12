# 0014. Mozilla's roots in batches of 32

- Status: Proposed
- Date: 2026-11-12
- Supersedes: [0013](0013-mozilla-roots.md), decision 2 (all of Mozilla's roots in
  one folder). ADR 0013's other decisions stand.

## Context

ADR 0013 imported all 118 of Mozilla's roots, with the user's CA, from one
folder, `/config/dreadovania/trusted-public/`. It asked the console two
questions: how long that import takes, and whether the ssl service has a limit.

On the maintainer's Switch (firmware 23.5.0, app 0.8.0), pairing through
Cloudflare failed with:

> Error importing ca path file sdmc:/config/dreadovania/trusted-public/mozilla-71.pem - libnx: 0x1B47B

That's module 123 (ssl), description 218. `mozilla-71.pem` is TunTrust Root CA,
an ordinary 4096-bit RSA root. The folder's files were written in order, so it
was the 72nd certificate imported into the connection. So the service most
likely limits a connection to about 71 imported certificates (or to their total
size). The backend stops at the first import error, so no Mozilla root helped at
all.

## Decision

1. **Batches:** Mozilla's roots are split into batches of at most 32, each in its
   own folder (`/config/dreadovania/trusted-public-0/`, `-1/` and so on), each
   with the user's CA. That's well under the limit seen, for count or size.
2. **The most widely used roots come first:** the first batch holds the roots
   behind most public certificates. Those are Let's Encrypt's ISRG roots,
   Google Trust Services and SSL.com (the three authorities Cloudflare uses),
   then DigiCert, GlobalSign, Sectigo, Amazon, Microsoft and GoDaddy. The list is
   `kPriorityRoots` in `core/src/api.cpp`. A host test fails if a certifi update
   renames or drops one of them, or if they stop fitting one batch.
3. **Order of attempts:** a connection first trusts the Switch's roots and the
   user's CA, as in ADR 0013. If the server's certificate isn't trusted, the app
   tries each batch in turn, and remembers the one that worked for that server
   until the app restarts. Every attempt verifies fully.
4. **The folders** are rewritten only when the bundle, the batch size or
   `ca.pem` changes. 0.8.0's single folder is removed.

## Consequences

- A server behind Cloudflare costs one failed handshake per app start, then
  works, because its root is in the first batch. A server whose root is in the
  last batch costs up to four.
- If the limit is lower than this Switch showed on other firmware, batches of 32
  still leave a wide margin. If even that fails, the error names the file, and
  the batch size is one constant.
