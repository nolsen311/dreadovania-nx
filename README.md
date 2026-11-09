# Dreadovania NX

An all-in-one Nintendo Switch homebrew app (NRO) for **Metroid Dread**
randomizer seeds. It connects to a
[Dreadovania Server](https://github.com/nolsen311/dreadovania-server) to
generate seeds, download them, and swap them in and out with one action.

> **Status:** planning. Nothing here is buildable yet. See [`docs/PLAN.md`](docs/PLAN.md).

## Planned features

- Pair with your server once, using a short code. No typing passwords on the Switch.
- Browse your seeds, or generate a new one from a preset.
- Download a seed's mod and install or swap it, showing which seed is active.
- Installs seeds the same way as the server's "Send to Switch", so the two can be mixed.
- (Later) Back up and swap save data per seed.

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
