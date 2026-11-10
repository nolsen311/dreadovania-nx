# 0007. Installing a seed: a manifest and one tar, staged and checked

- Status: Accepted
- Date: 2026-11-10
- Amends: [0004](0004-phase4-plan.md), decision 3 (per-file downloads)

## Context

ADR 0004 planned to download a patched seed file by file. A seed is several
hundred files, and each HTTPS connection on the Switch costs a handshake through
its TLS service. What matters is that an install is all-or-nothing.

## Decision

As in dreadovania-server ADR 0018:

- The app reads `GET /seeds/{id}/mod/manifest` (each file's path, size and
  SHA-256) and streams `GET /seeds/{id}/mod.tar` (plain ustar, same files, same
  order) into `/config/dreadovania/staging/`, checking every file's size and
  SHA-256 as it arrives. Any mismatch, extra or missing file, or write failure
  stops the download and discards the staging folder; the card isn't touched.
- Only then does it install, as Send to Switch does: write the SD-card record
  (`/config/dreadovania/installed.json`, "installing") first, remove the previous
  seed's files that this one doesn't replace, move the staged files into place,
  then record "installed". Another mod's files are never replaced without asking,
  and nothing outside Dread's two mod folders is ever deleted.
- The app keeps one connection open for all its requests.
- The tar reader, SHA-256, the plan and the record are in `core/`; the install
  steps themselves (`source/installer.cpp`) are also run in the host tests against
  a temporary folder, with a stand-in for the network.

## Consequences

- An interrupted download costs a re-download (about 25 MB); an interrupted
  install is finished or cleaned up by the next one, as with Send to Switch.
