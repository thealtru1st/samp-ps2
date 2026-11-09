# SA-MP for PS2 (PCSX2) - plan

Goal: a real SA-MP 0.3.7 client running inside GTA San Andreas PS2 under PCSX2,
able to join ordinary SA-MP / open.mp servers alongside PC players, with an
in-game server browser (join by IP, server list, live query info).

Target binary: SLES-52927 (PAL-G) v1.00, CRC A3EF1321,
ELF sha256 524da2d3...ddfe. No symbols, so all game addresses are found by RE
of this exact file. Other regions are out of scope until this one works.

## Architecture

    PCSX2 (DEV9 Sockets adapter, PS2 = 192.0.2.100, gw 192.0.2.1)
      EE, payload at 0x82000000 (128MB ExtraMemory mode, never touched by the game)
        hook      per-frame call from the game's main loop
        smap.c    bare-metal SMAP driver via EE window 0xB4000000 -> DEV9 regs
        net.c     ethernet / ARP / IPv4 / UDP (+ minimal TCP for HTTP list)
        rak.c     RakNet (SA-MP flavour): cipher, cookie, reliability, RPC
        samp.c    join flow, RPC dispatch, sync packets
        game.c    glue to game: read local ped, drive remote peds/vehicles
        ui.c      server browser + chat, drawn with the game's own font code

Key decisions, each checked against source:

- No IOP modules. Memory.cpp maps EE 0x14000000 to DEV9read/write, so the EE
  drives the adapter directly and GTA's IOP audio/streaming setup is untouched.
  Consequence: this runs on PCSX2 only, not on a real PS2.
- Payload in the extra RAM (ExtraMemory), so the game's 32MB heap is untouched.
- Delivered by `.pnach` `bytes` patches: no modified ISO/ELF, and nothing from
  the game is redistributed. The user's own disc stays as it is.
- Protocol written fresh in C from open.mp's RakNet (MPL-2.0) and the SA-MP
  wire format. ref/samp-source has no licence, so it is read for reference only
  and its code is not copied.
- The protocol library is portable: it is compiled for the host first and tested
  against a real server there, before it ever runs on the EE.

## Milestones (each has a pass condition that is observed, not assumed)

| # | Milestone | Passes when |
|---|-----------|-------------|
| 0 | Toolchain, server, emulator config | EE gcc builds; open.mp server runs; PCSX2 boots game with Eth + 128MB + PINE |
| 1 | Injection | payload heartbeat counter increments in game, read back over PINE |
| 2 | Network from EE | EE sends SA-MP query, server's reply is parsed in payload |
| 3 | RakNet + join (host build) | host build of rak/samp appears as a connected player in server log |
| 4 | Join from PS2 | same code on EE; server log shows the PS2 player joined and spawned |
| 5 | Local sync | server sees PS2 player's real position move while playing |
| 6 | Remote players/vehicles | server-side bot/PC player visible and moving in PS2 game |
| 7 | Browser UI | join by IP + list + query info work using the pad |
| 8 | RPC coverage | chat, dialogs, textdraws, checkpoints, objects, pickups, etc. |
| 9 | Stock samp-server | repeat 4-6 against the original 0.3.7 server, not just open.mp |

## Status (2026-11-07)

All streams merged and on (feat.c). Seen working in the real game against private open.mp
servers: join, chat, browser, join by IP, failure reporting, keyboard typing, password
dialog and login, server skins, spawn, weapons, respawn, driving a server car, objects,
pickups, checkpoint, gang zone, 3D labels, time/weather, a 130-object grid at 25 fps.
A public stock server accepted the auth handshake (computed answer, 256/256 vectors).

Not yet seen in game: 5 other dialog styles, textdraws, menus, scoreboard, name tags,
passengers, remote drivers, trailers, remote weapons, actors.
Not tested at all: a real PC client, stock samp-server end to end, public server play.

Rebuild flow: tools/w tools/build.sh; python tools/mkpnach.py; python tools/make_state.py.
Host fakes cannot catch game timing or memory: two bugs found only in game were the spawn
weapon race and the streaming memory gate.

## Known hard limits (stated up front)

- Server-side anti-cheat that inspects PC client memory (ClientCheck-style
  checks, SAMPCAC and the like) cannot be answered truthfully by a PS2 client,
  so servers that enforce those will reject it. These will not be spoofed.
- Custom models (0.3.DL) and PC-only files have no PS2 equivalent.
- PS2 map/model data differs slightly from PC; objects/models that exist only on
  PC will be missing.
- Public server lists are mostly HTTPS; the EE has no TLS. See M7.
