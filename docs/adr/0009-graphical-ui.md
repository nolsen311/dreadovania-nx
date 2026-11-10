# 0009. A graphical UI drawn with SDL2

- Status: Proposed
- Date: 2026-11-10

## Context

The app is a text console (libnx's `consoleInit`): 80 columns, one colour,
button hints at the bottom. It works, but it's hard to read in handheld mode
and doesn't show state at a glance. The mockups in `docs/design/` show the
intended screens.

The options on the Switch:

1. **SDL2 with SDL2_ttf**, drawing our own few widgets. Already in the pinned
   devkitPro image (portlibs: SDL2, SDL2_ttf, SDL2_image, FreeType), so no new
   download or pin. Hardware-accelerated through its OpenGL ES backend.
2. **A homebrew UI framework** (Borealis, Plutonium). Ready-made widgets and the
   system look, but a new dependency to vendor and pin, with its own build, and
   a look that's the Switch's rather than the project's.
3. **deko3d or OpenGL directly.** Full control, far more code.

## Decision

1. **SDL2 + SDL2_ttf**, from the pinned image. No new third-party code is added.
2. **The console's own font.** Text is drawn with the Switch's shared system
   font (libnx `plGetSharedFontByType`), so no font file is bundled and
   accents and other alphabets in preset names work. The monospace face for seed
   hashes is the same font; hashes are separated by wider spacing instead.
3. **A small widget layer of our own** in `source/ui/`: text, card, list row,
   chip, toggle, progress bar, button-hint bar, dialog, toast. Screens are
   functions of the app's state, redrawn each frame (60 fps cap; idle frames
   skipped). Touch input selects and activates rows.
4. **Logic stays where it is.** `core/` (parsing, checks, install planning) and
   `source/jobs.cpp` / `source/installer.cpp` don't change; long work runs on a
   worker thread and reports progress to the screen through a small queue, so
   the UI stays responsive (the console UI blocks while it works).
5. **Original art only**: the project's hexagon icon, and shapes drawn in code
   (check marks, button glyphs). No Nintendo or game artwork.
6. **The look follows the server's dark theme** (colours, chips, wording), so
   the two read as one product.

## Pull requests

1. This ADR and the mockups.
2. The renderer and widget layer, the shared font, and the Library screen; the
   other screens still use the text console until replaced.
3. New seed, Working, Pair, Confirm and the installed toast; remove the text
   console.

Each comes with a hardware checklist: Claude can build the app but can't run it
on a Switch.

## Consequences

- The `.nro` grows by SDL2's size (a few MB); the build is unchanged apart from
  linking SDL2 and SDL2_ttf.
- Screens can be tested on the host only as far as their state; drawing is
  checked on hardware.
