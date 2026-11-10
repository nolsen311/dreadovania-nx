# Graphical UI: design mockups

Mockups for the app's graphical UI ([ADR 0009](../adr/0009-graphical-ui.md)), at
1280×720, the Switch's handheld resolution. Each is an HTML page (with the
shared `mockups.css`) and a PNG rendered from it. The icon is the project's own
artwork, the same as the server's.

| Screen | What it shows |
|---|---|
| [Library](1-library.png) | Your seeds, with their state; what's installed on this Switch |
| [New seed](2-new-seed.png) | Pick a preset, read its description, race on/off |
| [Working](3-working.png) | Generate → patch → download and check → install, with progress |
| [Pair](4-pair.png) | Server address and the 8-character code, in two groups of four |
| [Confirm](5-confirm.png) | Asking before replacing another mod's files |
| [Installed](6-installed.png) | The outcome, as a toast over the library |

## Principles

- **The same look as the server's web UI** (its dark theme): one accent colour,
  rounded cards, status chips with the same words (Patched, Patching, Failed…).
- **Big enough for handheld mode:** body text 22 px, nothing under 15 px,
  the selection obvious from across the room (accent border and glow).
- **Every screen ends in a bar of button hints**, using the controller's own
  letters, so nothing has to be remembered. Touch works too: tap a row to
  select it, tap again to act.
- **Say what happens before it happens**, and what changed after: the steps on
  the working screen, the toast with the file count, and asking before
  touching another mod's files.
- Seed hashes (the in-game words) are shown in a monospace face, as on the
  server, so they're easy to compare with the game's own screen.

The names, hashes and server address in the mockups are made up.

To re-render the PNGs after changing a mockup, with Playwright installed:

```bash
node -e "const{chromium}=require('playwright');(async()=>{const b=await chromium.launch();const p=await b.newPage({viewport:{width:1280,height:720}});for(const f of process.argv.slice(1)){await p.goto('file://'+require('path').resolve(f));await p.screenshot({path:f.replace('.html','.png')})}await b.close()})()" docs/design/*.html
```
