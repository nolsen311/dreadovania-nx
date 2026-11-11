# 0010. Several servers, picked by the network the Switch is on

- Status: Proposed
- Date: 2026-11-11

## Context

The app pairs with one Dreadovania server and keeps its address and device
token in `/config/dreadovania/device.json` (ADR 0004). The maintainer uses more
than one server: for example one at home and one at a friend's, where the friend
has invited them (server ADR 0020). Each needs its own pairing, and switching
between them today means unpairing and pairing again.

Which server to use usually follows from where the Switch is, and the Switch
knows which network it's connected to. libnx's `nifmGetCurrentNetworkProfile`
returns the current network profile: its name and, for Wi-Fi, its SSID. It is
available through `nifm:u`, the user-level service that homebrew already uses,
and it only reads. The app already initialises the network through libnx's
socket setup.

## Decision

1. **The app keeps a list of paired servers.** Each entry has the server's
   address, the device ID, name and token from pairing (as now), the networks
   it's linked to, and when it was last used (a counter, not a clock). The list
   is `/config/dreadovania/servers.json`.
   - **Upgrade:** if `servers.json` doesn't exist and `device.json` does, its one
     server becomes the first entry, and `device.json` is removed once
     `servers.json` is written, so the token is only stored once. Going back to
     an older app means pairing again.
   - Pairing with a server that is already in the list replaces its entry (new
     token), keeping its linked networks. Unpairing removes it.
2. **Networks are identified by name.** For Wi-Fi, the SSID; on a wired
   connection (docked with a LAN adapter), the network profile's name. Nothing
   else about the network is read or stored, and the names stay on the SD card:
   they are never sent to a server.
3. **Choosing the server at start-up, and whenever the app refreshes:**
   1. the server linked to the current network, if there is one;
   2. otherwise, the only server, if there is just one;
   3. otherwise, the one used most recently.
   A network is linked to at most one server: linking it to another moves it.
   When the choice isn't by network, the Library says so ("Not linked to this
   network") so it is never a surprise which server is in use. If the network
   can't be read (offline, or the call fails), the choice falls back to 2 and 3.
4. **Linking happens on the Switch, with a button.** Pairing while connected to
   a network links that network to the new server, unless the network is
   already linked to another. A **Servers** screen lists the paired servers and
   lets the user switch to one, link the current network to it, or unpair it,
   and pair another.
5. **TLS is unchanged** (ADR 0006): every server is checked against the
   Switch's trusted roots and the user's own CAs in `ca.pem`, which may hold
   several certificates, one per server's CA. Verification is always on, for
   every server.
6. **Installed seeds stay as they are.** The SD card holds one installed seed at
   a time, whichever server it came from; the install manifest already records
   which files it installed, so switching servers needs no change there.

## Pull requests

These are stacked: each builds on the one before.

1. This ADR.
2. The server list: reading, writing and the upgrade from `device.json`, with
   host tests; the app uses the list, choosing by rules 2 and 3 only.
3. The network: reading the current network's name, the choice by network
   (rule 1), and linking on pairing, with host tests for the choice.
4. The Servers screen and the Library's server line, with preview images.

## Consequences

- Moving between home and a friend's server needs no typing: open the app and
  it uses the server for that network.
- A network with the same name in two places (a common default SSID) can only
  be linked to one server; the user switches by hand on the other.
- Device tokens for several servers are stored on the SD card, each as secret
  as the single one is today. Unpairing a server removes its token and revokes
  it on the server.
- An older app can't read `servers.json`; downgrading means pairing again.
