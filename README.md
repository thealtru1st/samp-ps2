# samp-ps2

**San Andreas Multiplayer, finally on the PS2.**

A proof-of-concept SA-MP 0.3.7 client that runs *inside* GTA San Andreas for the
PlayStation 2, under the PCSX2 emulator. It joins ordinary SA-MP and open.mp servers,
next to PC players, from an in-game server browser.

> **Status: proof of concept.** It has joined private open.mp test servers and a busy
> public roleplay server, and you can play on them: chat, log in, spawn, drive, shoot,
> and see everyone else. It is also unfinished, slower than the PC client on big servers,
> and can still freeze. Expect rough edges.

No game files are included or modified. The client is delivered as a PCSX2 patch
(`.pnach`) that you build yourself, and it runs on your own copy of the game.

## What works

- **Server browser** in the game: the public SA-MP server list (sam.markski.ar), favourites, join by IP or
  hostname, live player counts and ping, nickname editing.
- **Joining**: the full RakNet handshake and SA-MP auth challenge, server passwords,
  class selection, spawning.
- **Sync**: your on-foot and in-car movement goes out, and other players and their
  cars come in, with weapons, damage and death.
- **Server features** (all implemented; a few, like menus and some dialog styles, are
  so far only covered by host tests): chat, all dialog styles, textdraws, game text, 3D text labels,
  name tags with health and armour bars, objects (moving and attached), pickups,
  checkpoints and race checkpoints, gang zones, map icons, time and weather, NPC actors,
  vehicle components and colours, the scoreboard.
- **Big servers**: PC model ids are mapped to their PS2 ids. The game heap is extended
  into PCSX2's extra memory, from 24 MB to 72 MB, so the thousands of objects that
  roleplay maps use fit.
- **50 FPS** (the PAL game is normally capped at 25); `E` in the browser toggles it.

## Limits

- **PCSX2 only.** The client drives PCSX2's emulated network adapter directly from the
  PS2's main CPU, bypassing the IOP the game keeps busy. A real PS2 would need a
  different network driver.
- **One disc only:** *Grand Theft Auto: San Andreas*, PAL, SLES-52927 v1.00, CRC
  `A3EF1321` (the German "En,De" release). Every game address was reverse-engineered
  from that exact executable.
- **Models only PC has** (SA-MP's added objects, ids 18631 and up, and custom models)
  don't exist on the PS2 disc and are skipped.
- **Client-side anticheat** that inspects PC memory can't be answered by a PS2, so
  servers that enforce it will kick you. It isn't spoofed.
- Busy areas on large servers can drop to 25–35 FPS.

## Requirements

- **PCSX2 2.x** (developed on a 2.x Qt build on Windows) and a PS2 BIOS you dumped.
- **Your own disc image** of the version above.
- **WSL** (Ubuntu) or Linux with the [ps2dev toolchain](https://github.com/ps2dev/ps2dev)
  (`mips64r5900el-ps2-elf-gcc`) to build the client.
- **Python 3** on Windows for the tools.

## Build

```sh
# In Git Bash on Windows (tools/w runs a script inside WSL):
tools/w tools/build.sh        # compiles payload/ into private/payload.bin
python tools/mkpnach.py       # writes Documents/PCSX2/patches/SLES-52927_A3EF1321.pnach
```

`PS2DEV` picks the toolchain (default `~/ps2dev-sdk/ps2dev`), `WSL_DISTRO` the WSL
distribution (default `Ubuntu`) and `PCSX2_PATCHES` the patch folder. On Linux, run
`tools/build.sh` directly.

## Set up PCSX2

1. **Settings → Advanced:** enable **128 MB RAM** (dev console mode, `ExtraMemory`).
   The client lives in that extra RAM, and so does the bigger game heap.
2. **Settings → Advanced:** enable **PINE** on slot **28011**. The keyboard helper and
   the test tools talk to the game through it.
3. **Settings → Network & HDD:** enable **Ethernet**, device type **Sockets**, device
   **Auto**.
4. Keep **patches enabled** (the default). The `.pnach` has no named section, so PCSX2
   always applies it.
5. Recommended, in the game's properties: **Fast CDVD**, as the client streams a lot of
   models.
6. Optional: `python tools/set_controls.py` (with PCSX2 closed) maps the pad to a
   PC-style layout: WASD, mouse to aim and fire, Space sprint, Shift jump, F enter car,
   T chat, Enter start. It backs up your config first.

## Play

```powershell
$env:GTASA_ISO = 'D:\isos\GTA San Andreas (Germany) (En,De) (v1.00).iso'
$env:PCSX2_EXE = 'C:\pcsx2\pcsx2-qt.exe'

python tools/make_state.py    # boots the game to the browser and saves savestate slot 9
.\tools\play.ps1              # starts from slot 9, with the keyboard helper
```

The patch skips the intro and the story missions, so the game boots straight to free
roam with the server browser open. Rerun `make_state.py` after every build, since a
savestate holds the client as it was when saved.

**In the browser:** `Enter` join, `Shift` add a server by IP or hostname, `F` change
nickname, mouse click to favourite, `Tab` switch lists, `Q` refresh, `E` toggle
25/50 FPS.
**In game:** `T` chat (`/commands` work), `V` reopen the browser.

**Typing:** the PS2 has no keyboard. `tools/kbd.py` (started by `play.ps1`) watches for
an open text box and, while PCSX2 has focus, writes what you type into it through PINE.
It stores and logs nothing. Without it, text boxes fall back to picking letters with the
pad.

## How it works

```
PCSX2  (Ethernet: Sockets API, PS2 at 192.0.2.100)
  EE (PS2 main CPU)
    game main loop ──jal──> stub ──> payload at 0x82000000 (extra RAM) ──> original call
      smap.c     bare-metal driver for the emulated SMAP adapter (DEV9 registers)
      net.c      Ethernet, ARP, IPv4, UDP, a small TCP, DNS
      rak.c      RakNet as SA-MP uses it: cipher, reliability, ordering, RPCs
      samp.c     join flow, sync packets; samp_rpc.c decodes every SA-MP RPC
      game.c     the game side: peds, skins, models, memory, script commands (scm.c)
      feat_*.c   combat, vehicles, dialogs and HUD, world (objects, pickups, labels…)
      ui.c       the browser, drawn with the game's own font and sprite code
```

- The `.pnach` writes the payload into the extra RAM and hooks three calls: once per
  frame before the game updates, in the 2D render pass, and at the pad read. No disc
  file is changed.
- Game functions and data were found by reverse engineering the SLES-52927 executable,
  which has no symbols. Notes are in `wip/*/re-*.md`.
- The protocol code is portable C. It runs on a PC first (`host/`), against a real
  server, before it runs on the PS2.

More background is in [`PLAN.md`](PLAN.md).

## Repository layout

| Path | What it is |
|---|---|
| `payload/` | the client: everything that runs on the PS2 |
| `host/` | runs the protocol code as a PC program, for testing against a server |
| `tools/` | build, patch, launch, keyboard helper, PINE and reverse-engineering helpers |
| `wip/<area>/` | test suites per area: host tests with a fake game layer, live tests against a local open.mp server (`*.pwn` gamemodes), in-game checklists, RE notes |

Host tests: `tools/w wip/world/run_tests.sh` (likewise `vehicle`, `combat`, `dialog`,
`rpc`, `rak`). The live tests need an open.mp server, which isn't included. The browser
suite's network rig also needs the PCSX2 sources and isn't part of this repository.

## Credits

- [open.mp](https://github.com/openmultiplayer): its RakNet port is the reference for
  the wire protocol, and `payload/sampdata.h` is generated from it (MPL-2.0, see
  [`NOTICE`](NOTICE)). Its server was used for testing.
- ReSAMP (MIT): `payload/authkey.c` follows its published auth-challenge transform
  (`sampnet/samp_auth.cpp`).
- [SAMonitor](https://sam.markski.ar) for the HTTP server list the browser reads.
- [PCSX2](https://pcsx2.net) for the emulator and its PINE interface, and the
  [ps2dev](https://github.com/ps2dev) toolchain.
- Sanny Builder's opcode database, used by `tools/scmdump.py` (not included).

## Disclaimer

Not affiliated with or endorsed by Rockstar Games, Take-Two Interactive, the SA-MP team,
open.mp or the PCSX2 team. *Grand Theft Auto* and *San Andreas* are trademarks of Take-Two
Interactive. This repository contains no game code, data or assets: you need your own
legally obtained copy of the game and your own BIOS dump. Respect the rules of the servers
you join.

## License

MIT, see [`LICENSE`](LICENSE), except `payload/sampdata.h`, which is MPL-2.0 (see
[`NOTICE`](NOTICE)).
