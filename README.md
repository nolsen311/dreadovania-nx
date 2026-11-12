# Dreadovania NX

An all-in-one Nintendo Switch homebrew app (NRO) for **Metroid Dread**
randomizer seeds. It connects to a
[Dreadovania Server](https://github.com/nolsen311/dreadovania-server) to
generate seeds, download them, and swap them in and out with one action.

> **Status:** early. The app pairs with a server, lists its seeds, generates and
> patches new ones from a preset, and installs them, with a graphical UI
> (see [`docs/design`](docs/design)).
> See [`docs/PLAN.md`](docs/PLAN.md) and [ADR 0004](docs/adr/0004-phase4-plan.md).

## Planned features

- Pair with your server once, using a short code. No typing passwords on the Switch.
- Browse your seeds, or generate a new one from a preset.
- Download a seed's mod and install or swap it, showing which seed is active.
- Installs seeds the same way as the server's "Send to Switch", so the two can be mixed.
- (Later) Back up and swap save data per seed.

## Installing

1. Download `dreadovania.nro` from the latest
   [release](https://github.com/nolsen311/dreadovania-nx/releases/latest), and
   check it came from this repository's release workflow:

   ```bash
   gh attestation verify dreadovania.nro --repo nolsen311/dreadovania-nx
   ```

2. Copy it to `/switch/` on the SD card.
3. **If you copy over USB** (hekate's USB mass storage, or similar), eject the SD
   card on the computer before you leave USB mode. The computer holds writes back,
   and leaving USB mode first can leave the file half-written: the right size, with
   part of it zeros. A damaged file looks like a broken app: it closes as soon as
   it starts, or shows error 2168-0002 from the Album.
4. If the app won't start, check the copy on the card. Mount the card again (so
   the computer reads the card, not what it cached) and compare with the release's
   `dreadovania.nro.sha256`:

   ```bash
   sha256sum /path/to/sd/switch/dreadovania.nro
   ```

   If it differs, copy the file again and eject before leaving USB mode.

Updates from inside the app (below) check the download's SHA-256 before
installing it, so they don't need these steps.

## Using it

1. Your server must be reachable over **HTTPS** at a hostname
   (see the server's `docs/https.md`).
2. On the server, open **Account → Switch app → Pair a Switch**.
3. In the app, press **A**, then enter the server's address and the code.

Press **R** for a new seed: choose a preset (**X** makes it a race seed, with
no spoiler on the server) and press **A**. The server generates the seed and
patches it with your game files, and the app installs it. It takes a minute or
two; **B** stops waiting, and the server carries on.

In the seed list, press **A** on any seed to get it onto the Switch: the app does
whatever it still needs (finish generating, patch, install). It downloads the
patched files, checks every one, and only then replaces the previous seed (other
mods' files are kept; it asks before replacing any). `*` marks the installed seed.

Patching needs your game files on the server (its **Game files** page).

**Updates:** when your server has a newer app, the seed list shows it; press
**−** to update in place. **ZL** checks for one right away (the server asks GitHub
at most every 5 minutes).

**Several servers** ([ADR 0010](docs/adr/0010-several-servers-by-network.md)): press
**L** for the Servers screen. **Y** pairs another server; pairing links the Wi-Fi
you're on to it, and the app then uses that server whenever the Switch is on that
network. **X** links or unlinks the current network, **A** switches to a server by
hand, and **R** unpairs one. On a network that isn't linked, the app uses the
server you used last, and says so at the bottom of the seed list.

The touchscreen works too in handheld mode: tap a row to select it, tap it again
to act on it.

To check the screens' layout on a computer, `tools/preview` draws each one with
sample data and saves it as a PNG (it needs SDL2, SDL2_ttf, SDL2_gfx and
SDL2_image, and a font file):

```bash
cmake -S tools/preview -B build-preview && cmake --build build-preview
```

```bash
SDL_VIDEODRIVER=offscreen DREADOVANIA_FONT=/path/to/font.ttf build-preview/preview /tmp
```

The app keeps its settings in `/config/dreadovania/servers.json`. If your server
uses a certificate from your own certificate authority, copy that CA's
certificate (and any intermediates) to `/config/dreadovania/ca.pem`: the app
trusts it in addition to the Switch's own trusted authorities, which cover
the common public ones (Let's Encrypt, Google Trust Services and others).
Certificates are always checked
([ADR 0016](docs/adr/0016-switch-roots-only.md)).

**Secure remote access** (the server's ADR 0023): if your server only lets in
devices with a certificate from it, pairing gives the Switch one, and the app
presents it on every connection and renews it by itself
([ADR 0012](docs/adr/0012-client-certificates.md)). Pair through the address the
server shows next to the pairing code. To use a certificate from your own CA
instead, put it on the SD card as `/config/dreadovania/certificates/<host>.p12`,
with its password in `<host>.password` (for `https://dread.example.com:8443`, the
host is `dread.example.com_8443`).

## Building

The app is built with devkitPro's libnx in its container image, pinned by
digest in [`.github/workflows/build.yml`](.github/workflows/build.yml). From
the repository folder:

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282 make
```

That writes `dreadovania.nro`: copy it to `/switch/` on the SD card and open it
from the Homebrew Menu. The first build in a fresh checkout also builds the
app's own libcurl, with a small patch for the Switch's TLS service
([ADR 0015](docs/adr/0015-rsa-cipher-and-own-curl.md)). That step needs
autoconf, automake and libtool. As root in the container (leave out
`-u "$(id -u):$(id -g)"` the first time), the script installs them itself.

The code that doesn't need a Switch (under `core/`) is unit-tested on your
computer with CMake and a C++20 compiler:

```bash
cmake -S tests -B build-host && cmake --build build-host && ctest --test-dir build-host
```

## Credits

[SimpleModManager](https://github.com/nadrino/SimpleModManager) by nadrino
inspired the idea of keeping a library of mods on the Switch. Dreadovania uses
none of its code (ADR 0004).

## How this project is built

This project is written by [Claude](https://claude.ai/code), Anthropic's AI
coding assistant, under the oversight of its maintainer
([@nolsen311](https://github.com/nolsen311)). Every change goes through a pull
request that the maintainer reviews, signs off under the
[Developer Certificate of Origin](CONTRIBUTING.md#developer-certificate-of-origin),
and merges. Claude never merges, never tags releases, and never signs off on
anyone's behalf. See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CLAUDE.md`](CLAUDE.md).

## What this project does *not* do

Patching happens on your server, using game files **you supply yourself** from
your own legally obtained copy. This repository and its releases contain **no game
content, keys, or copyrighted assets**.

Dreadovania is an unofficial fan project. It is not affiliated with or endorsed
by Nintendo or the Randovania team. All trademarks belong to their owners.

## License

[GPL-3.0-or-later](LICENSE).
