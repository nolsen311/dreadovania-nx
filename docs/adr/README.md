# Architecture Decision Records

Short records of significant decisions. Each one has a status, the context, the
decision itself, and its consequences. To change a decision, add a new ADR that
supersedes the old one; don't rewrite history.

| # | Title | Status |
|---|---|---|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-claude-authorship-and-dco.md) | Claude authorship, maintainer sign-off (DCO) | Accepted |
| [0003](0003-signoff-by-comment.md) | Sign off and merge with a `/signoff` comment | Accepted |
| [0004](0004-phase4-plan.md) | Phase 4 plan: what the app is built from and how it installs seeds | Proposed |
| [0005](0005-trusted-roots-and-json.md) | Trusted roots from certifi, and a small JSON reader of our own | Accepted; roots superseded by 0006 |
| [0006](0006-switch-tls-service.md) | Certificates are checked by the Switch's TLS service | Accepted |
| [0007](0007-install-from-manifest-and-tar.md) | Installing a seed: a manifest and one tar, staged and checked | Accepted |
| [0008](0008-release-by-comment.md) | Release with a `/release` comment and a release app | Accepted |
| [0009](0009-graphical-ui.md) | A graphical UI drawn with SDL2 | Proposed |
| [0010](0010-several-servers-by-network.md) | Several servers, picked by the network the Switch is on | Proposed |
| [0011](0011-signoff-stacked-pull-requests.md) | `/signoff` for stacked pull requests | Proposed |
| [0012](0012-client-certificates.md) | Client certificates for secure remote access | Proposed |
| [0013](0013-mozilla-roots.md) | Mozilla's root store, imported one certificate at a time | Proposed |
| [0014](0014-root-batches.md) | Mozilla's roots in batches of 32 | Proposed |
