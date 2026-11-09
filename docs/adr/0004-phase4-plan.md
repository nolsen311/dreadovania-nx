# 0004. Phase 4 plan: what the app is built from and how it installs seeds

- Status: Proposed
- Date: 2026-11-09

## Context

`docs/PLAN.md` left several questions open: whether to reuse SimpleModManager,
which UI toolkit to use, how to make verified HTTPS requests on the Switch, and
how to install a seed. Since then the server gained HTTPS and passkeys (server
ADRs 0004 and 0005) and "Send to Switch" over FTP (server ADR 0016), which
installs seeds directly into Atmosphere's folders with a manifest. The app
should behave the same way, so both can be used on one console.

Findings:

- **SimpleModManager** (nadrino/SimpleModManager, checked at
  `8c662d1`) is licensed **GPL-3.0**. Its source files carry no "or any later
  version" notice, so its code must be treated as GPL-3.0-only; reusing it would
  make this app GPL-3.0-only too. Its install logic copies files and compares
  them with what's on the card; it keeps no record of what it installed, so it
  can't cleanly remove a seed that changes only some files. Its library layout
  is `/mods/<game>/<mod>/<tree under /atmosphere>`. It uses its own fork of
  Borealis for its UI.
- **Seeds are partial:** a seed holds only the files it changes (exlaunch mode,
  about 25 MB), so installing one over another mixes them unless the previous
  seed's files are removed first. The server's push solves this with a manifest
  (server ADR 0016).

## Decision

1. **No SimpleModManager code.** The app is our own code, GPL-3.0-or-later.
   SimpleModManager is credited in the README as the inspiration for the
   library idea.
2. **Install like Send to Switch, with the same manifest.**
   - A seed is installed directly into `atmosphere/contents/010093801237c000/`
     and `atmosphere/exefs_patches/DreadRandovania/`.
   - The files Dreadovania installed are recorded in
     `/config/dreadovania/installed.json`, in the same format the server writes,
     so a seed sent over FTP and a seed installed by the app replace each other
     cleanly.
   - The same safety rules: nothing outside those two folders is ever deleted,
     whatever the manifest says; another mod's file is never overwritten unless
     the user chooses to; the planned files are recorded before the card changes,
     so an interrupted install is cleaned up next time.
   - A **SimpleModManager-compatible library** (keeping several seeds under
     `/mods/` to swap between) is deferred. Re-downloading a seed is quick, and
     the manifest covers what swapping needs.
3. **Downloads are per file, not a zip.** The server gets an additive API that
   lists a patched seed's files (path, size, SHA-256) and serves each one. The
   app checks every file's hash after download, and needs no zip code. The
   server's zip download stays for manual installs.
4. **HTTPS:** libcurl with mbedTLS, from devkitPro's portlibs.
   - Certificates are always verified, against **Mozilla's root store as
     published by the curl project** (`cacert.pem`, MPL-2.0), bundled in the
     app's romfs. CI downloads a pinned version and checks its SHA-256;
     updating it is a reviewed pull request.
   - A user with their **own certificate authority** (a home lab CA) can put its
     certificate at `/config/dreadovania/ca.pem`. It is added to the trusted
     roots; verification stays on. There is no option to skip verification.
5. **UI:** the first versions use libnx's text console, behind a small screen
   interface, so pairing, browsing and installing can be built and tested on
   hardware early. A graphical toolkit is chosen in a later ADR after a short
   spike; Borealis (Apache-2.0) is the leading candidate.
6. **Pairing and access** follow server ADR 0004: a short code from the web app,
   exchanged for a device token stored at `/config/dreadovania/device.json`.
   The token can list and generate seeds, read presets, and download patched
   files; nothing else. The server records the device's name and address, and
   the web app can revoke it.
7. **Build:** C++20 with libnx, built in the `devkitpro/devkita64` container
   image pinned by digest. Code that doesn't touch the Switch (the manifest,
   path checks, the install plan, JSON parsing) lives in a library that is also
   compiled and unit-tested on the CI runner. Releases follow the server's
   model: `.nro` plus SHA-256 checksums and a build-provenance attestation,
   from tags only.

## Consequences

- The app and Send to Switch are interchangeable for installing a seed.
- Without zip handling and with hashes checked per file, a dropped Wi-Fi
  connection costs one file, not the whole download.
- The text UI is plain at first; that's the price of reaching hardware testing
  early.
- The server needs two additions before the app can download: device tokens
  (server ADR 0017) and the file-listing API.
- Claude can build and unit-test the app in CI, but can't run it on a Switch: every
  pull request that changes behaviour on the console comes with a checklist for
  the maintainer.

## Pull requests

1. This ADR.
2. Server: device pairing and tokens (server ADR 0017).
3. App skeleton: the libnx build in a pinned container, host unit tests, CI and
   the release workflow; the app starts and shows its version.
4. App: pair with the server and list seeds.
5. Server: the per-file mod API. App: download and install a seed.
6. App: generate a seed from a preset.
