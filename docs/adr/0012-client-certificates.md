# 0012. Client certificates for secure remote access

- Status: Proposed
- Date: 2026-11-12

## Context

A Dreadovania server can turn on **secure remote access** (server
[ADR 0023](https://github.com/nolsen311/dreadovania-server/blob/main/docs/adr/0023-secure-remote-access.md)):
it then only answers devices that present a client certificate it gave them, apart
from its front door (pairing, invites). Pairing hands the Switch its certificate
with its token, as a PKCS#12 file (`.p12`) and a password; the app renews it
through `POST /api/v1/devices/me/certificate` once `GET /api/v1/devices/me` says
it's due. An owner whose proxy checks against their own CA can instead give the
Switch a certificate from that CA.

The app's HTTPS is curl with devkitPro's libnx backend, which uses the Switch's
own TLS service. Its source (devkitPro's `switch-curl.patch`, checked against the
pinned package's SHA-256) loads a client identity from the file in
`CURLOPT_SSLCERT`, in PKCS#12 only, with the password in `CURLOPT_KEYPASSWD`,
through `sslContextImportClientPki`.

## Decision

1. **One certificate per paired server**, kept on the SD card in
   `/config/dreadovania/certificates/<device id>.p12`. Its password, fingerprint,
   expiry and renewal time are kept in that server's entry in `servers.json`, next
   to its token and as secret.
2. **Pairing saves it.** The pairing request itself is sent without any
   certificate (it may be through the server's front door), and the server's main
   address from the answer (`server_url`) is the one kept, since the front door
   serves nothing else.
3. **Every request to a server presents that server's certificate**, and only
   that one: switching servers changes it and starts a fresh connection, so one
   server's certificate is never sent to another.
4. **Renewal is automatic.** Once per server each session, after a successful
   refresh, the app asks `GET /devices/me`; when the server says it's time (a third
   of the lifetime left, or it now gets certificates from another CA), it fetches
   the replacement, writes it next to the old one and switches. A Switch paired
   before certificates gets its first one the same way. If it fails, the current
   certificate keeps working and the app tries again next session.
5. **A certificate from the owner's own CA** wins when it's on the SD card:
   `/config/dreadovania/certificates/<host>.p12` with its password in
   `<host>.password` (the host as in the server's address, `:` becoming `_`). The
   app never renews those; the owner's CA does.
6. **Unpairing deletes the server's `.p12`.** TLS verification of the server is
   unchanged (ADR 0006).

## Consequences

- With secure remote access on, a paired Switch keeps working, at home or away, and
  renews its certificate without the owner doing anything.
- A Switch paired with an older app has no certificate: it gets one at its next
  refresh while secure remote access is still off, or it must be paired again.
- The `.p12` and its password are on the SD card, as secret as the token already
  is: someone with the card can use the Switch's access until it's unpaired or the
  certificate is revoked on the server.
- The client side of the TLS service can't check a revocation list (the backend has
  it disabled), which only matters for servers, not for clients.
