# UI reverse engineering: text, panels, draw hook, pad

Binary: `private/SLES_529.27` (SLES-52927 v1.00). Static analysis only, nothing here was run.
`ui_game.h` next to this file has the defines and typedefs, and it compiles with the payload's
flags. Its `ui_*` bridges were checked in the disassembly: text in `a0`, x in `$f12`, y in `$f13`.

Confidence: **High** means I read the behaviour in the code, usually with independent callers
agreeing. **Medium** means the code fits but something is inferred. **Unconfirmed** is listed in
section 8. Names are borrowed from the PC game for readability; the behaviour is what was verified.

## 1. Read this first: five things that will bite

1. **The payload's C ABI is not the game's.** The game is EABI: ints count `a0-a3,t0-t3` and
   floats count `$f12+` independently. `tools/build.sh` passes no `-mabi`, so gcc 15.2 emits **N32**
   (`.mdebug.abiN32`), where ints and floats share one slot counter. Compiled and checked:
   - `void(float,float,const char*)` gives `$f12,$f13,$a2`
   - `void(const char*,float,float)` gives `$a0,$f13,$f14`

   The game wants `$a0,$f12,$f13`, so **no C prototype can call PrintString**. All-int and
   all-float signatures are fine. Use the `ui_PrintString`, `ui_GetNumberLines` and
   `ui_GetTextRect` inlines in `ui_game.h`.
2. **You cannot draw from the existing hook at 0x246770.** `CFont::InitPerFrame` runs right after
   it (0x246778) and resets the font buffer, and the frame has not rendered yet. A second hook is
   needed, see section 4.
3. **Text is deferred and rectangles are immediate.** DrawRect goes straight out; PrintString
   queues. Draw order within a layer is rect, text, `DrawFonts()`.
4. **PrintString can draw immediately too.** The queue is only 512 bytes (0x7C25F0-0x7C27F0, 0x30
   header per line) and it flushes itself when full (0x2A6B24) or when the font texture slot
   changes (0x2A67AC). Render state must already be valid when you call PrintString.
5. **`_` renders blank, and `~` and a leading `*` are special.** See section 3. This matters for
   SA-MP names like `Carl_Johnson` and for server text.

## 2. CFont

All setters write globals in the state block at 0x7C2390. Coordinates are virtual 640x448: the
game draws with literal 640.0/448.0 (0x2AEDAC) and PrintChar clips to those bounds (0x2A59DC-0x2A5A0C).

How it was reached: opcode handler 800-899 is 0x310240 and its compare chain gives the handlers.
0x033E-0x0349 only fill the `IntroTextLines` array (0x6F94E0, stride 0x44, 96 entries, count at
gp+0x430). The reader of that array is `CTheScripts::DrawScriptText` at **0x2AEBF0**, called with
1 from 0x2B0274 and 0 from 0x2B03B0. It calls every setter in order with the field the opcode
stored, which names each one.

| Address | Function | Signature (regs) | Evidence | Conf. |
|---|---|---|---|---|
| 0x2A6EB0 | PrintString | `a0`=text, `$f12`=x, `$f13`=y | 0x2AEDD4 passes x/y in `$f12/$f13` and buffer in `a0`. Returns early on `text[0]` `*` or NUL (0x2A6ED8) | High |
| 0x2A8890 | SetScale | `$f12`=w, `$f13`=h | stores 0x7C2394/98. DrawScriptText passes `(+0, +4 * 0.5)` | High |
| 0x2A88B0 | SetScaleForCurrentLanguage | same | w*0.8 when byte 0x6FFA4C is 1-4 | High |
| 0x2A8990 | SetColor | `a0`=`CRGBA*` | reads bytes 0-3 through `a0`, so a pointer, not by value. Applies AlphaFade | High |
| 0x2A8A80 | SetFontStyle | `a0`=style | 2 gives slot 0 + remap 2; 3 gives slot 1 + remap 1; else slot=style. From +0x28, set by 0x0349 | High |
| 0x2A8CE0 | SetOrientation | `a0`: 0 centre, 1 left, 2 right | writes flags 0x7C23A9/AA. 0x2AEC90: right flag +0x24 gives 2, centre flag +0xD (0x0342) gives 0, else 1 | High |
| 0x2A8CD0 | SetJustify | `a0`=bool | from +0xC, set by 0x0341 | High |
| 0x2A8AF0 | SetWrapx | `$f12` | from +0x10, set by 0x0343. Left-aligned wrap limit | High |
| 0x2A8B00 | SetCentreSize | `$f12` | from +0x14, set by 0x0344. Centred wrap width | High |
| 0x2A8B10 | SetRightJustifyWrap | `$f12` | stores 0x7C23C0, read on the right path at 0x2A7188 | High |
| 0x2A8C20 | SetDropShadowPosition | `a0`=px | sets 0x7C23C6, clears edge | High |
| 0x2A8C40 | SetEdge | `a0`=px | sets 0x7C23CB/CC, clears shadow. `SetEdge(0)` after use at 0x2AEDDC | High |
| 0x2A8B30 | SetDropColor | `a0`=`CRGBA*` | stores 0x7C23C7..CA | High |
| 0x2A8C60 | SetProportional | `a0`=bool | from +0x1C, set by 0x0348 | High |
| 0x2A8C70 | SetBackground | `a0`=on, `a1`=framedBox | from +0xE, set by 0x0345. `a1=0` uses DrawRect, `a1=1` uses the frontend box 0x23E520 | High |
| 0x2A8C90 | SetBackgroundColor | `a0`=`CRGBA*` | stores 0x7C23B4..B7 | High |
| 0x2A8B20 | SetAlphaFade | `$f12` | SetColor scales alpha while this is below 255 | High |
| 0x2A8980 | SetSlant | `$f12` | stores 0x7C239C, consumed at 0x2A6470 | High |
| 0x2A8960 | SetSlantRefPoint | `$f12`,`$f13` | stores 0x7C23A0/A4 | Medium (no callers) |
| 0x2A7930 | GetStringWidth | `a0`=text, `a1`=full, `a2`=script, returns `$f0` | with `full=0` it stops at the first space (0x2A7F10). Pass 1 and 0 | High |
| 0x2A7060 | GetNumberLines | `a0`=text, `$f12`,`$f13` | 0x2AF14C. Wrapper over 0x2A7080 with print=0 | High |
| 0x2A77B0 | GetTextRect | `a0`=rect, `a1`=text, `$f12`,`$f13` | called by PrintString at 0x2A6F64 | High |
| 0x2A7080 | ProcessCurrentString | `a0`=print, `a1`=text, `$f12`,`$f13` | wrapped by 0x2A7060 and 0x2A7070 | High |
| 0x2A6010 | RenderFontBuffer | none | walks 0x7C25F0 up to the pointer at gp+0xB08, then resets it | High |
| 0x2A86B0 | DrawFonts | none | `j 0x2A6010`. 11 callers, incl. the ends of both 2D render functions | High |
| 0x2A5970 | InitPerFrame | none | resets buffer pointer and font id to -1. **Do not call it** | High |

### Before and after calling

- **Before:** nothing needs initialising per call, but **only drop shadow resets per frame**
  (0x2A5978). The rest persists from whichever game code ran last, so set everything every time:
  scale, alpha fade (255.0) before colour, colour, style, orientation, wrap, proportional, justify 0,
  slant 0, and shadow or edge.
- **Background is one-shot:** PrintString clears 0x7C23AB after drawing it (0x2A701C).
- **After:** call `DrawFonts()`. The game's flush at the hook site would cover this anyway, but an
  explicit flush keeps later panels above text.
- **State isolation (suggested, not tested):** queued lines copy their state at PrintString, so you
  can save 0x40 bytes at 0x7C2390 on hook entry and restore on exit. The game then never sees our
  settings.
- **Sizing:** line height is `18 * scaleY` (0x2A74C0: `(32*sy)/2 + 2*sy`). The game's subtitles use
  style 1 at scale 0.58 x 1.22 (gp-relative constants, 0x2AEF74).
  Reasonable start for chat: style 1, proportional on, about 0.45 x 0.95. That size is my guess.
- **Line length:** keep one line under about 450 bytes. The flush check only makes room for `len+52`
  in a 512-byte buffer, so anything longer overruns the sprites at 0x7C27F0.

Worked example in the game: `CHud::DrawSubtitles` 0x2AEE10, which sets background off, proportional,
orientation, shadow, drop colour, scale, then prints.

## 3. Text encoding

**Plain ASCII works directly. No conversion is required.**

- Text is 8-bit: every read in PrintString, GetStringWidth and RenderFontBuffer is `lbu` with a
  1-byte step, and glyph index is `char-32` (0x2A6428).
- **AsciiToGxtChar is 0x2A5470** (`a0`=src, `a1`=dst): a byte copy with a NUL, no translation. The
  game's own ASCII path shows the pattern at 0x2F3100-0x2F3130: sprintf into 0x6FF680, 0x2A5470 into
  0x69FDA0, PrintString. It is safe to skip. Confidence High.

Characters to handle before printing server text:

| Input | Behaviour | Evidence |
|---|---|---|
| `_` | blank glyph, in both drawing and width (index 63) | 0x2A5B64, 0x2A7C30 |
| `~` | starts a token up to the next `~`. Unknown letters are swallowed too | 0x2A7FB0, table 0x64D480 |
| `*` first | whole string is skipped | 0x2A6EE0 |
| 0x80 and up | index 156+ becomes blank on styles 0/1 | 0x2A644C |
| lowercase | styles 2 and 3 fold to capitals through 0x2A86C0, so use style 0 or 1 for chat | 0x2A8804-0x2A8818 |

Replace `~` and handle `_` yourself (for example print `-`, or draw a small rect). Colour tokens are
`~r~ ~g~ ~b~ ~w~ ~h~ ~y~ ~p~ ~l~ ~s~`, `~n~` is a newline, and the rest are button icons. There is no
`{RRGGBB}` support, so SA-MP colours mean separate PrintString calls, advancing x by
`GetStringWidth(seg, 1, 0)`.

Other helpers, all High: GXT strcpy 0x2A5520, strcat 0x2A54B0, strlen 0x2A5560, in-place upper
0x2A55A0, FilterOutTokensFromString 0x2A85F0 (in place, but copies to a 256-byte stack buffer, so
limit input). TheText at 0x69F310 with `Get` 0x18ED90 (`a0`=this, `a1`=key). Avoid the game's sprintf
0x550F90; it is varargs across the ABI gap, and `util.c` can do this itself.

## 4. Per-frame draw point

**Redirect the `jal` at 0x247318.** Original word `0x0C0AA1AC` (`jal 0x2A86B0`), delay slot at
0x24731C is `nop`. Resident: the program headers put the fixed segment at 0x100000-0x88D900, and
all nine overlay segments load at 0x88D900.

The frame, read from the main loop:

```
0x246770 CSprite2d::InitPerFrame       (our existing hook, tail-calls it)
0x246778 CFont::InitPerFrame           (resets the font buffer)
0x24678C CGame::Process                (0x24338C UpdatePads is its first call)
0x2467F4 skip world if menu active or camera faded out, else
0x246A74   Render2dStuff 0x246FF0      (CHud::Draw 0x2B0030 ... DrawFonts at 0x2472A8)
0x246AE0 RenderMenus 0x2472D0
0x246B10 DoFade 0x2460E0
0x246B18 Render2dStuffAfterFade 0x247300:
           0x247308 CHud::DrawAfterFade 0x2B02C0
           0x247310 CMessages::Display(0)
           0x247318 DrawFonts   <-- hook here
           0x247338 overlay call, only under a flag, not ours
0x246B44 camera end-update (0x39B3B0, paired with 0x39B3C0 before the render)
```

Why this site: it is the last 2D work in the frame, so it lands over world, HUD, pause menu and
fades. It runs on both the world and skip-world paths, while 0x2472A8 does not, and it sits inside
the camera update. Only 0x246AF4 skips it, when byte 0x6FFA2C is 1. That byte is set at 0x246BE4
and looks like a load or restart flag, but I did not trace it. Confidence High for structure. The
CHud, DoFade, camera and fade-status names are Medium, but the site does not depend on them.

Hook body:

```c
CFont_DrawFonts();                       /* flush game text so our panels cover it */
RwRenderStateSet(RW_STATE_ZTESTENABLE, 0);
RwRenderStateSet(RW_STATE_ZWRITEENABLE, 0);
RwRenderStateSet(RW_STATE_SRCBLEND, RW_BLEND_SRCALPHA);
RwRenderStateSet(RW_STATE_DESTBLEND, RW_BLEND_INVSRCALPHA);
RwRenderStateSet(RW_STATE_FOGENABLE, 0);
RwRenderStateSet(RW_STATE_VERTEXALPHA, 1);
/* panels, then text, then: */
CFont_DrawFonts();                       /* also replaces the displaced call */
```

The explicit states copy what Render2dStuff sets at 0x246FF0-0x24706C. That function does not run on
the skip-world path, so the blend state cannot be assumed there.

Wiring notes:

- `jal` only reaches 0x0xxxxxxx, so it needs a low-RAM stub like the one at 0xFFF00, plus a second
  entry in the payload at a known address. `crt.S` only has `_start`.
- Reusing the payload stack is safe: the two hooks never nest. Keep `sp` 16-byte aligned, because game
  code uses `sq`/`lq`.
- N32 only preserves even `$f20-$f30`, and `crt.S` saves no FP registers. The main loop only keeps
  `$f20` live (prologue at 0x2466BC), so this is fine at these sites. Don't assume so elsewhere.
- Pass only CPU-read data to game functions. Strings and rects from 0x82xxxxxx are fine since they
  are copied. Nothing DMA-read should come from payload memory.

## 5. Filled rectangles

| Address | Function | Signature | Evidence | Conf. |
|---|---|---|---|---|
| 0x2B2120 | CSprite2d::DrawRect | `a0`=`CRect*`, `a1`=`CRGBA*` | clears raster, SetVertices, alpha on only if a != 255, Im2D trifan with 4 verts, alpha off. Letterbox bars at 0x24718C | High |
| 0x2B21F0 | DrawRect, 4 colours | `a0`=rect, `a1,a2,a3,t0`=`CRGBA*` | same, but leaves VERTEXALPHA alone | High; corner order unverified |
| 0x2B1600 | SetVertices | `a0`=rect, 4 colours | fills 0x7C3370 | High |
| 0x2A5450 | CRGBA ctor | `a0`=this, `a1,a2,a3,t0`=r,g,b,a | 302 callers. A plain struct is simpler | High |

Rect layout is `{left, y_bottom, right, y_top}`, with **+4 holding the larger y**: the top bar at
0x247170 stores 0, 44, 640, 0. No scaling, raw 640x448 values. Both are all-int calls, so N32 is
fine. 0x2B0AD0 (polygon, eight floats plus a pointer) is not callable from N32 without a bridge.

Engine pointers, read then call: RenderStateSet at 0x825050 (`a0`=state, `a1`=value), Im2D render
primitive at 0x825060. State numbers fit the RenderWare enum at every call site read: 1 raster, 6
ztest, 8 zwrite, 9 filter, 10/11 blend with 5/6, 12 vertex alpha, 14 fog.

## 6. Controller

`CPad` array 0x700BC0, stride 0x94, pad 1 at 0x700C54. Field layout, High unless noted. Fully decoded
from `CPad::Update` 0x247A10, which is the **only** caller of scePadRead (0x247F40).

`NewState` is at **+0x00** and `OldState` at **+0x28**: each state is **0x28** bytes, not the PC's
0x30. 0x247A38 onward copies New to Old field by field.

| Off | Field | Raw source | SCM id |
|---|---|---|---|
| +0x00 | LeftStickX | byte 6 | 0 |
| +0x02 | LeftStickY | byte 7 | 1 |
| +0x04 | RightStickX | byte 4 | 2 |
| +0x06 | RightStickY | byte 5 | 3 |
| +0x08 | L1 | mask 0x0004 | 4 |
| +0x0A | L2 | 0x0001 | 5 |
| +0x0C | R1 | 0x0008 | 6 |
| +0x0E | R2 | 0x0002 | 7 |
| +0x10 | DPad Up | 0x1000 | 8 |
| +0x12 | DPad Down | 0x4000 | 9 |
| +0x14 | DPad Left | 0x8000 | 10 |
| +0x16 | DPad Right | 0x2000 | 11 |
| +0x18 | Start | 0x0800 | 12 |
| +0x1A | Select | 0x0100 | 13 |
| +0x1C | Square | 0x0080 | 14 |
| +0x1E | Triangle | 0x0010 | 15 |
| +0x20 | Cross | 0x0040 | 16 |
| +0x22 | Circle | 0x0020 | 17 |
| +0x24 | L3 | 0x0200 | 18 |
| +0x26 | R3 | 0x0400 | 19 |

Three sources agree: the raw masks match libpad's bit layout, `CRunningScript::GetPadState` 0x30A9A0
maps SCM id to `offset = id*2` (used by 0x00E1 at 0x1FEB88), and 0x0494 reads +0..+6 as the sticks
(0x33238C).

Values:

- Sticks: `raw-128`, 0 inside +-42, then scaled by 127/85 (0x248348 onward). Range is roughly
  -127..127.
- Buttons: 0 or 255 in digital mode. In pressure mode (mode nibble 9, 0x24811C) D-pad and face
  buttons are pressure*4 clamped to 255, and shoulders are raw pressure. **Test for non-zero, never
  255.**
- Just-pressed is `New != 0 && Old == 0`.

Rest of the struct:

| Off | Field | Evidence | Conf. |
|---|---|---|---|
| +0x50..0x63 | steering history | 0x248F84 | High |
| +0x64 | history index | 0x248F88 | High |
| +0x68 | u8 pad state machine | jump table at 0x247B10 | High |
| +0x6A | u16 Mode (config 0-3) | every getter branches on it | High |
| +0x6C / +0x70 | shake duration / frequency | 0x247E38, 0x2479C4 | High |
| +0x6E | u16 DisablePlayerControls | getters return 0 if non-zero, see below | High |
| +0x71..0x76 | 5-entry history + index | 0x248C08 | Medium |
| +0x77 | u8 JustOutOfFrontEnd | game sets 25 on leaving its menu (0x23B090), decremented at 0x248DBC, checked by enter-car and 0x00E1 | High |
| +0x79..+0x7F | per-action disable bytes | gate individual getters | Medium |
| +0x80 | u32 LastTimeTouched | 0x248BD8 | High |

Other functions: `GetPad` 0x248F00 (`a0`=index, High), `UpdatePads` 0x2479D0 (both pads, High),
`Clear` 0x247640 (`a0`=pad, `a1`=enableControls, `a2`=resetPhase, High).

### Blocking the game while the menu is open

**Recommended: redirect the `jal` at 0x24338C.** Original word `0x0C091E74` (`jal 0x2479D0`), first
call in `CGame::Process`, with `sq s0,0(sp)` in the delay slot which must stay. New target calls
0x2479D0, copies pad 0's NewState to a payload shadow, and if the UI is open zeroes +0x00..+0x4F:

```c
ui_padstate *p = (ui_padstate *)CPAD_ARRAY;
shadow_old = shadow_new;
shadow_new = p[0];                       /* the stride is 0x94: index by byte offset for pad 1 */
if (ui_open) memset((void *)CPAD_ARRAY, 0, 0x50);
```

Everything in the game sees an idle pad, including Start, which is what DisablePlayerControls cannot
do. UpdatePads has three other callers (0x237728, 0x240B64, 0x240F58), in 0x236770, 0x2409C0 and
0x240DC0. These have their own draw loops and look like frontend screens, and they bypass this hook.

**Closing the menu:** Old will be 0 while a button may still be held, so it would register as
freshly pressed. Keep zeroing until all buttons are released, then set `pad[+0x77] = 25` as the game
does at 0x23B090.

**Alternative: DisablePlayerControls only.** Needs no new hook: OR a private bit into +0x6E from the
existing tick, read NewState/OldState there, and clear the bit on close. Bit 0x8000 is free: the
masks in use are 1, 4, 8, 0x20, 0x80, 0x100 (from every masked write to +0x6E). It must be
re-asserted each tick because 0x247734 and four sites in `CGameLogic::Update` store zero. Known gaps,
because these read pad state without checking +0x6E:

| Address | Reads | Effect |
|---|---|---|
| 0x23ADB0 | Start | **opens the pause menu** |
| 0x24A200, 0x24A2A0, 0x24A320 | Select, D-pad up/down | not identified |
| 0x2491D0, 0x249300 | left stick, D-pad | not identified |
| 0x21B130 | right stick X | not identified |
| 0x3048F0 | Cross | not identified |
| 0x513B90, 0x3AC6D0 | L1; Square, Cross | not identified |

The menu code at 0x23BA00, 0x23ED50, 0x52C680, 0x52CB60 and 0x59D000 also reads directly. Script
`SET_PLAYER_CONTROL` (0x01B4) reaches `MakePlayerSafe` 0x264B60, which sets bit 0x20 but also changes
ped and world state, so don't use it for this.

## 7. Text entry

There is no on-screen keyboard: the ELF has no `keyboard`, `usbkb`, `sceKbd` or text-entry strings,
and I found no name-entry routine. The UI needs its own grid or character-cycling input built from
section 6. Useful bits: width measurement via GetStringWidth for a caret, and the GXT string helpers
in section 3. The ELF's sprintf should be avoided as noted.

## 8. Not confirmed

- **Nothing was run.** The hook order and drawing are read from code, not observed. First runtime
  test: one DrawRect and one PrintString at 0x247318, in world and with the pause menu open.
- **Which texture each font slot holds.** The loader isn't in the ELF: nothing references gp+0xB00
  beyond shutdown and rendering, and 0x2A55F0 (FONTS.DAT reader) has no `jal` caller, so both likely
  live in an overlay. The `.sdata` order font2, font2m, font1, font1m suggests slot 0 is font2 and
  slot 1 is font1. Check how styles 0 and 1 look at runtime.
- **Blend state on the skip-world path.** Hence the explicit sets in section 4. I did not read DoFade
  or the frontend's state handling.
- **Stub space.** 0xFFF00-0xFFF43 is proven in use. 0xFFF50 upward is probably free but I did not
  prove that nothing else touches low RAM there.
- **The save and restore of CFont state** is reasoned from the queue code, not tested. The block's
  last bytes (0x7C23CD-CF) have no known use.
- **PAL output.** The 640x448 space is what the code uses. How it maps to the display wasn't traced.
- **Corner order** of 0x2B21F0, and the function names marked "not identified" in section 6.
