# Dreadovania NX: project plan

Status: accepted (2026-11-07). This is **Phase 4** of the overall plan. The
server-side plan and decision records live in
[`dreadovania-server/docs`](https://github.com/nolsen311/dreadovania-server/tree/main/docs);
see in particular ADRs 0004 (auth), 0006 (Switch delivery) and 0007 (separate repo).
Items marked **(verify)** are open questions.

## Scope

- **Pairing:** a short code from the server's web UI is exchanged for a long-lived
  device token, stored on the SD card. The app refreshes the Switch's IP on the
  server each time it launches.
- **Seeds:** list seeds, generate one from a preset, show its details (spoiler log
  only behind an explicit reveal step).
- **Download:** fetch the patched mod over **HTTPS with certificate verification**,
  using a bundled root CA store. No "ignore certificate errors" option.
- **Install and swap:** each seed is a mod in a SimpleModManager-compatible library
  (`sd:/mods/<Game>/<Mod name>/...` **(verify)**). Applying one installs it to
  `atmosphere/contents/010093801237C000/`. The UI shows the active seed.
- **Save data (later):** back up and swap the Dread save alongside each seed
  **(verify how Dread save slots behave)**.
- **Not in scope:** patching on the Switch. The server always patches.

## Contract with the server

- The server's OpenAPI spec (`/api/v1`) is the contract. Pin the spec version the
  NRO is built against and check compatibility in CI.
- Device tokens are narrowly scoped: list seeds, download mods, generate from a preset.

## Reusing SimpleModManager

- **To verify:** its license (expected GPL-3.0; this repo is GPL-3.0-or-later, so GPL-3.0-only code would make the combined app GPL-3.0-only), how
  cleanly its mod apply/remove logic separates from its UI, and whether its UI
  framework (Borealis) suits us.
- **Options:**
  1. A git submodule pinned to a commit.
  2. A vendored copy pinned to a commit.
  3. Same layout, our own code.
- Credit the project clearly and send improvements upstream where possible.

## CI/CD baseline

- Build in the devkitPro container image, **pinned by digest**.
- Every action pinned to a full SHA, with `permissions: {}` at the top level and
  permissions granted per job.
- Protection on `main` via [`.github/rulesets/protect-main.json`](../.github/rulesets/protect-main.json),
  and admin-only release tags via `protect-release-tags.json`, both applied by `scripts/bootstrap.sh`.
- DCO sign-off model and Claude working rules from `claude-project-template` (ADR 0002).
- Dependabot for Actions, Docker digests and submodules.
- Releases built from tags only, on GitHub-hosted runners: `.nro` plus SHA-256 checksums
  plus a build-provenance attestation (`actions/attest-build-provenance`).
- A content guard that rejects game files and keys (same rules as the server repo).

## Branding

"Dreadovania" on the home menu. No "Metroid" in names, and original icon art only
(no Nintendo artwork or Samus imagery).
