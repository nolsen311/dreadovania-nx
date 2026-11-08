# Working rules for Claude

These rules apply to every Claude session and workflow in this repository.
They exist so that the project's history is a trustworthy audit trail.

## Authority and sign-off

- **Never add a `Signed-off-by:` line** to any commit, for anyone. A sign-off is a
  personal legal certification under the Developer Certificate of Origin. The
  maintainer signs off on Claude-authored changes when merging.
- **Never merge a pull request, approve one, or create or push a tag.** Those are
  the maintainer's actions, even when tools would technically allow them.
- **Never post a `/signoff` comment**, or anything that starts with `/signoff`.
  It merges with the maintainer's sign-off (ADR 0003); only they may post it.
- **Never change repository settings, rulesets, environments or secrets.**
  Propose changes in a pull request (for example to `.github/rulesets/`) and
  let the maintainer apply them.
- Commit as `Claude <noreply@anthropic.com>`, so authorship is visible in history.
  End commit messages with the `Co-Authored-By:` and session trailers.

## How changes are made

- Never push to `main`. Work on a branch and open a pull request.
- Keep pull request descriptions complete: what changed, why, and how it was
  verified. They are the durable record; session links are not.
- Record significant decisions as ADRs in `docs/adr/`. To change a decision,
  add a new ADR that supersedes the old one.
- Never weaken a check to get CI green: no skipped, disabled or quarantined tests,
  and no loosened rulesets or scanner thresholds without the maintainer's
  explicit agreement in the pull request.
- Report results faithfully. If something wasn't verified, say so.

## Supply chain

- Pin every GitHub Action to a full commit SHA, with the version in a comment.
- Workflows start from `permissions: {}`; grant permissions per job.
- Pin container images by digest, and verify digests against the registry
  rather than trusting a single lookup.
- New dependencies need a stated reason in the pull request.

## Source files

- New source files start with an SPDX header:
  `SPDX-License-Identifier: GPL-3.0-or-later`.
- Never commit secrets, keys, credentials or personal data.

## This project

- This is Phase 4 of the Dreadovania plan. The server repo
  ([`dreadovania-server`](https://github.com/nolsen311/dreadovania-server))
  holds the overall plan and ADRs; this repo's scope is in `docs/PLAN.md`.
- **Content policy (absolute):** never add game content, console keys, or links
  to them. Patching always happens on the server, from files the user supplies.
- **Branding:** "Dreadovania". No "Metroid" in names, and original icon art only.
- **API contract:** the server's `docs/openapi.json` is the contract. Build
  against a pinned version of it, never against undocumented behaviour.
- **TLS:** always verify certificates, using a bundled root CA store. Never add
  an option to ignore certificate errors.
- **SimpleModManager:** stay compatible with its library layout; check the
  license of any code reused from it and credit it.
