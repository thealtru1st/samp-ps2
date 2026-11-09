# World stream: in-game tests

None of this has been run in PCSX2. Host tests cover the logic and the server side, not the
game. Each step below has what to expect and which guess it proves.

## Setup

1. Set `FEAT_WORLD` to 1 in `payload/feat.c`, and add the `main.c` call from the report's
   Integration list. Without that call the world only clears at the next join.
2. `tools/w tools/build.sh`, then `python tools/mkpnach.py`, as for any payload.
3. Start the test server. This does not touch the main server:
   `powershell -File wip\world\start_ingame_server.ps1`
4. In the game: join `127.0.0.1:7881`. If the Sockets adapter cannot reach 127.0.0.1, rerun
   with `-Bind 0.0.0.0` and use the address that reaches the main server.
5. Type `/next` in chat for each step, or `/step N` to jump. Steps start at Grove Street, and
   each one puts you back there.
6. When finished: `powershell -File wip\world\start_ingame_server.ps1 -Stop`

Read `ST->log` over PINE as usual if something goes wrong; the world code logs refused models
and audio streams there.

## Before any step

| Check | Expect | If not |
|---|---|---|
| Boot to the join menu and wait 30 s | No Respect box, no other help box, no help sound | A fourth help path exists; look for writers of 0x7C3010 |
| Game runs after boot | No crash at the first frame | Runtime patches of code at 0x2A9200 are the first suspect |
| Top right of the HUD | No clock | 0x5FE9E0 not the format in use |

## Objects

| Step | Do | Expect | Proves |
|---|---|---|---|
| 1 | Look around | A bin 5 m ahead, a crate to the left. Road bridge 40 m north, in the air. Nothing at the third spot, on the right | Create and rotation. Bridge proves the PC to PS2 id map. Missing model skipped, no crash |
| 2 | Watch the bin | Jumps 2 m toward you, then slides 10 m north in about 10 s. Crate turns to 135 | Set pos, set rot, timed move |
| 3 | Watch the crate | Moves and turns, and stops after 3 s, part way | Move with rotation, stop |
| 4 | | All gone | Destroy |
| 5 | Wait 15 s and turn around | A grid of bins around you, reaching about 40 to 60 m. No crash, frame rate holds. Dynamic map objects and pickups still appear | The 130 cap, nearest first, 56 pool slots kept free. **Note the frame rate** |
| 6 | You move 400 m south | No bins near you. `/step 6` again from Grove Street brings none back until you walk north | Streaming out |
| 7 | Wait 5 s | No crash | Mass destroy |
| 8 | Walk | Bin 1.5 m above your head, moving with you. Info icon over the head, health icon near the right hand. **Expect the hand one not to swing with the arm**: there is no bone tracking | Attach to player. Approximate bones |
| 9 | | All gone | Detach and destroy |

## Pickups

| Step | Do | Expect | Proves |
|---|---|---|---|
| 10 | Walk 4 m north-east onto the heart | Chat says `PICKUP n`. Health as in the game. Armour pickups scattered around you, up to 96 at once. No help box | Create, ring read, RPC 131, cap |
| 11 | | All gone | Destroy |

## Checkpoints

| Step | Do | Expect | Proves |
|---|---|---|---|
| 12 | Walk to the red cylinder at +10, +10 | Radar blip, red cylinder. `ENTER checkpoint` on walking in, `LEAVE checkpoint` on walking out | `00FE` drawing the sphere from our thread, called every frame |
| 13 | | Checkpoint replaced by a race one with an arrow toward +30, +30. Walk in: `ENTER race checkpoint` | `06D5` and types |
| 14 | | Finish flag type | Type 1 |
| 15 | Look up | A ring in the air | Air type 3 |
| 16 | | Gone, blip gone | `06D6` |

## Radar

| Step | Do | Expect | Proves |
|---|---|---|---|
| 17 | Radar and pause map | Icon 56 near, a magenta dot on the left, icon 6 at the edge. A sphere at the fourth | All four `MAPICON_` styles, raw colour on `0165` |
| 18 | | All gone | `0164` |
| 19 | Radar | Red square, 200 m across, centred on you and clipped by the radar's circle. Turn: it turns with the radar | **`DrawAreaOnRadar` from the draw hook, the riskiest call here.** If the radar state is not valid at that point, the square will be misplaced or absent |
| 19 | Pause map | Not shown there. Known gap | |
| 20 | | Flashes red and green twice a second | Flash |
| 21 | | Gone | Hide |

## Labels

| Step | Do | Expect | Proves |
|---|---|---|---|
| 22 | Face the bin spot | "Red" in red, "green" in green, white second line below, centred, staying on the point as you turn. Walk 40 m off: it goes | `CalcScreenCoors` output really is 640x448. **If the text sits in the wrong place, check this first** |
| 22 | Walk behind CJ's house from the label "Behind the wall" | Cyan text hidden by the wall, shown from the open | `06BD` and the compare flag read from local 34 |
| 23 | | Gone | Hide |

## World state

| Step | Do | Expect | Proves |
|---|---|---|---|
| 24 | | Midnight, stays midnight. Foggy weather | `00C0`, the frozen clock, `01B6` |
| 25 | | Noon, sunny, HUD clock showing and running | `toggle_clock` |
| 26 | | Clock gone, time stops | |
| 27 | Jump | Long floating jump. Throw a car off something if one is around | All 33 sites patched without a crash. **If the game dies here, the recompiler did not take the code writes** |
| 28 | Jump | Normal | Restored |
| 29 | | A sound, then a second one, and an explosion 28 m away | `018C` and `0948`. **PS2 sound ids may not match PC: note what you hear** |
| 30 | Look south | CJ's house gone, and you can walk through where it was. Walk away 300 m and back: still gone | RemoveBuilding, the mapped id, LOD, IPL reload |
| 31 | | Dark room or empty void at interior 3, `INTERIOR 3` in chat | `04BB`, and RPC 118 from combat, or from the world code when combat is off |
| 32 | | Back outside | |

The time, weather and clock steps also run the combat stream's handlers when it is on. Both are
meant to agree.

## Actors

| Step | Do | Expect | Proves |
|---|---|---|---|
| 33 | | A ped 4 m ahead facing you, chatting animation, not walking off | `009A`, freeze, animation via `feat_combat_game.c` |
| 34 | | Moves 2 m, faces east, stops animating | Pos, angle, clear |
| 35 | Shoot it with the Deagle | `ACTOR HIT` in chat once per hit | `051A`, health delta, RPC 177 |
| 36 | | Gone | Stream out |

## After the steps

| Check | Expect |
|---|---|
| Disconnect from the menu | Without the `main.c` change, leftovers stay until the next join. With it, the world clears at once |
| Rejoin | Everything from the last session is gone |
| Join a real mapped server | No crash from thousands of objects; things appear near you. Missing pieces are SA-MP's own models, listed in the log |

Please report the frame rate at step 5 and any crash address. `LIVE_MAX` in
`feat_world_obj.c` and `POOL_RESERVE` beside it are the first things to tune.
