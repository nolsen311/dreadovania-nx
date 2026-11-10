# 0008. Release with a `/release` comment and a release app

- Status: Accepted
- Date: 2026-11-10

## Context

Releases are built by `release.yml` from `v*` tags (ADR 0004), but creating a
tag needs a clone or the GitHub web UI's release form. The server already
releases from a phone with a `/release` comment (dreadovania-server ADRs 0014
and 0015, from the project template's ADRs 0004 and 0005). The app should
release the same way.

## Decision

- Use the template's *Release by comment* workflow
  (`.github/workflows/release-tag.yml` and `.github/scripts/release-tag.js`),
  unchanged apart from this repository's setup: the version comes from the plain
  `VERSION` file.
- When the repository owner comments exactly `/release` on a merged pull
  request, the workflow tags its merge commit `vX.Y.Z`. It refuses, with a
  comment saying why, if the pull request isn't merged, its checks didn't pass,
  or the version is already released or isn't newer than the latest release.
- The tag is created by a GitHub App (the release app), with its private key in
  the `release-tag` environment, which only `main` can use. On a personal
  account a workflow's own token can't be exempted from the tag ruleset, and a
  tag created with that token wouldn't start `release.yml`.
- `scripts/bootstrap.sh` creates the `release-tag` environment and adds the
  release app to the tag ruleset's bypass list.
- Claude never posts `/release` (CLAUDE.md). It opens the pull request that
  bumps `VERSION`, and stops there.

## Consequences

- A release is: merge the version bump with `/signoff`, comment `/release`, then
  approve the `release` deployment.
- One-time setup by the maintainer: a release app (new, or the server's one
  installed here too), two repository variables, re-running `bootstrap.sh`, and
  the key as an environment secret (CONTRIBUTING.md, "Release app").
- Pushing a `v*` tag by hand still works.
