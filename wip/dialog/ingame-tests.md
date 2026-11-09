# In-game tests for the feat_dialog modules

None of this has been run in the game. The logic was tested on the host against a real
open.mp server (see the end), but everything that touches the game was only compiled:
fonts, rectangles, sizes, positions, blips and the camera. Each test below says what
it is really looking at.

## Before you start

1. Apply the Integration list. `python3 wip/dialog/integrate.py payload <dir>` writes the
   patched `samp.c`, `main.c`, `menu.c`, `menu.h`, `game.c` and `game.h` to `<dir>`. If an
   edit no longer matches it stops and names it.
2. `tools/w tools/build.sh`, then your usual pnach step.
3. Start the test server: `powershell -File wip\dialog\server.ps1 start`. It prints
   `running pid N on <ip>:7791`. It has its own copy of the binaries and does not touch
   the server on 7777.
4. Start `tools/kbd.py` beside the emulator so you can type.
5. In the browser press Shift and add `<ip>:7791`, then Enter to join. Nickname: anything.
   If the PS2 cannot reach that address, set `$c.network.bind` in `server.ps1` to one it can.

Keys (pad button in brackets), from `tools/set_controls.py`:

| Key | Button | In a dialog |
|---|---|---|
| Enter or Space | Start, Cross | left button, or open the text box |
| Left click or F | Circle, Triangle | right button, cancel |
| Arrows, or W/S | D-pad, left stick | move |
| Q / E | L2 / R2 | page up / down |
| Tab | L1 | hold for the scoreboard |

To finish: `powershell -File wip\dialog\server.ps1 stop`. Commands below are typed in chat
(T). Every server callback shows as a grey `CB ...` chat line, which is the real check.

## 1. Login dialog (the blocker)

| # | Do | Expect |
|---|---|---|
| 1.1 | Join | Dark panel in the middle of the screen, title `Login` in gold, blue title bar. Body: `Welcome to Dialog Test` with `Dialog Test` gold, `Please log in.`, `Enter your password:`. A lighter field reading `Press Enter to type`. Bottom row `Enter: Login` and `Click: Quit` |
| 1.2 | Try to walk with WASD, press Start | Player does not move and the pause menu does not open |
| 1.3 | Press Enter, type `wrong`, Enter | Text box opens titled `Login`, characters show as `#`. Then `CB dialog 1 1 -1 [wrong]`, and the dialog returns with `Wrong password.` in red |
| 1.4 | Enter, type `hunter2`, Enter | `CB dialog 1 1 -1 [hunter2]`, `CB logged in`, about a second later `CB reqspawn 1`, then `CB spawned`. You spawn at Grove Street and can walk |
| 1.5 | Reconnect, on the login dialog press Enter, then click to cancel the text box | Back on the dialog, nothing sent. Click again: `CB dialog 1 0 -1 []`, dialog returns |

If 1.1 shows nothing: check the log over PINE for `dialog: show 1 style 3`. If the line is
there the problem is drawing; if not, the RPC never arrived. If text is tiny or huge, the
scale in `feat_dialog_box.c` (`FONT_SX`, `FONT_SY`) is what to tune.

## 2. Other dialog styles

| # | Do | Expect |
|---|---|---|
| 2.1 | `/s1`, Down, Down, Up, Enter | List `Grove`, `Red row` in red, `Airport`, `Pier`, highlight bar moves. `CB dialog 10 1 1 [{FF0000}Red row]` |
| 2.2 | `/s1`, Down, Down, click | `CB dialog 10 0 2 [Airport]` |
| 2.3 | `/s2`, Down, Enter | Three aligned columns, no right button shown. `CB dialog 11 1 1 [Sawnoff]` |
| 2.4 | `/s3`, Down, Enter | Header row `Name Score Ping` in gold above the highlight. `Bob` is green. `CB dialog 12 1 1 [{00FF00}Bob]` |
| 2.5 | `/s4`, read, click | `nice` in red. The long line wraps inside the panel. `Tabbed` is indented. `CB dialog 13 0 -1 []` even with no right button |
| 2.6 | `/s5`, Enter, type `Sweet Johnson 100%`, Enter | Characters visible in the box this time. `CB dialog 14 1 -1 [Sweet Johnson 100%]` |
| 2.7 | `/s6`, wait | Dialog disappears by itself after 1.5s, `CB hidden`, pad returns to the game |
| 2.8 | `/s1`, press T, click to cancel the box, then press V and V again | Chat box and browser open over the dialog, which ignores the keys. Closing them returns to it with the same row selected |

Look at the column alignment in 2.3 and the wrap in 2.5 closely: widths came from a fake
on the host, so these are the first real measurements of the game's font.

## 3. Textdraws

| # | Do | Expect |
|---|---|---|
| 3.1 | `/s7` | Top centre: `Global` grey, `red`, `green`, second line `line two` light blue. Left: `BOXED TEXT` in capitals on a translucent blue box. Below it a solid orange rectangle |
| 3.2 | `/s8` | Top line becomes `Changed string`, `string` cream |
| 3.3 | `/s9` | All three go |
| 3.4 | `/s18` | A red block of `long word` across the top, no crash. This string would smash the stack through the game's own wrapping. It does not reach that code, but this is the proof on real hardware emulation |

Compare sizes with a PC client if you can: letter scale is taken from the PC client's draw
code and has not been checked against a real screen.

## 4. Selectable textdraws

| # | Do | Expect |
|---|---|---|
| 4.1 | `/s10` | `Click A`, `Click B`, `Click P` stacked. `Click A` yellow with a thin box around it. Hint at the bottom. The player stops moving |
| 4.2 | Enter | `CB click 1`. Still selecting |
| 4.3 | Down, Enter | Yellow moves to `Click B`. `CB click 2` |
| 4.4 | Down, Enter | `CB pclick 1` (a per-player textdraw) |
| 4.5 | Click | `CB click 65535`, hint gone, player can move |
| 4.6 | `/s10`, wait, `/s11` with chat (T still works) | Selection ends, `CB click 65535` |
| 4.7 | `/s12` | They disappear |

## 5. Menus

| # | Do | Expect |
|---|---|---|
| 5.1 | `/s13` | Panel at about (200, 120): title `Weapon shop`, gold headers `Item` and `Cost`, three rows. `Locked row` is grey |
| 5.2 | Down, Enter | Highlight skips the grey row to `Shotgun`. `CB menu row 2`, panel closes |
| 5.3 | `/s13`, click | `CB menu exit` |

## 6. Game text

| # | Do | Expect |
|---|---|---|
| 6.1 | `/s14` | Upper middle, large game font: `STYLE` red, `THREE`, then `SECOND LINE` under it, for 3 seconds |
| 6.2 | `/s15` | Seven `style N` texts at once for 6 seconds. All readable, none overlapping badly. Positions and sizes are my approximation, not the game's: tell me which look wrong next to a PC client |

## 7. Players (needs a second client)

Join with a second client as well. `tools/host.sh` only talks to 7777, so use
`~/samp-dialog-build/live_dialog <ip> 7791 Peer_One peer` from WSL (built by
`wip/dialog/run_live.sh`). It logs in, stands at Grove Street and leaves by itself after two and a half minutes.

| # | Do | Expect |
|---|---|---|
| 7.1 | Hold Tab | Scoreboard: server name, player count, your id, `You`, `Peer One`, score and ping within 3 seconds. You can still walk. Releasing closes it |
| 7.2 | `/s16`, hold Tab | Your row reads `Renamed Tester`, score 1234, in teal. Note the `_` shows as a space: the font has no such glyph |
| 7.3 | Walk to the peer | Name tag `Peer One (0)` above its head, tracking it as the camera moves. **The first real test of the projection**: if tags sit in the wrong place or mirror, look at `feat_dialog_proj.c` |
| 7.4 | Walk 80 metres away | Tag goes at the server's 70 metres |
| 7.5 | `/s17` near the peer | `Hello from a bubble` in magenta above the tag for 8 seconds, and the peer's blip turns red |
| 7.6 | Look at the radar | Peer has a blip in its colour. `CHANGE_BLIP_COLOUR` with an RGBA value is assumed to behave as on PC |

## 8. Robustness

| # | Do | Expect |
|---|---|---|
| 8.1 | `/s1`, then stop the server | On timeout the dialog goes, the browser opens and the pad works. Nothing is left on screen |
| 8.2 | `/s7`, `/s10`, leave, rejoin | Screen is clean after rejoin |
| 8.3 | `/s1`, press V, join another server | No dialog from the old session |
| 8.4 | Pause (Start) with `/s7` showing | Textdraws hide while paused and come back. Pause menu works normally |

## 9. A real public server

Only after 1-8 pass, and your call: the login flows that matter are a dialog over class
selection (covered), `SpawnPlayer` after login (allow=2, should work), and a server that
spectates you during login (`TogglePlayerSpectating`), which depends on the combat stream.

## What the host tests cover

`tools/w wip/dialog/run_tests.sh`: 195 checks and a fuzz of every handler under ASan and
UBSan. `tools/w wip/dialog/run_live.sh 7791`: the real `samp.c`, `rak.c` and these modules
against real open.mp 1.6.7, with a scripted player, 122 steps. `mutate_live.sh`: four
deliberate bugs, each caught. `check_integration.sh`: the Integration list links for the EE.
