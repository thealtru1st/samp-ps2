/* ui_game.h - GTA SA PS2, SLES-52927 v1.00 (PAL-G). Addresses found by static RE of
 * private/SLES_529.27; evidence and confidence levels are in wip/ui/re-ui.md.
 *
 * ABI WARNING. The game is EABI: ints count a0-a3,t0-t3 and floats count $f12+
 * independently. The payload compiler (mips64r5900el-ps2-elf-gcc 15.2, no -mabi) is N32:
 * ints and floats share one slot counter. A C prototype mixing the two therefore puts
 * arguments in the wrong registers:
 *     void (float,float,const char*)  -> $f12,$f13,$a2   (game wants the text in $a0)
 *     void (const char*,float,float)  -> $a0,$f13,$f14   (game wants $f12,$f13)
 * All-int and all-float signatures match, so their typedefs below are safe. The mixed
 * ones (PrintString, GetNumberLines, GetTextRect) must go through the ui_* inlines.
 */
#ifndef UI_GAME_H
#define UI_GAME_H

typedef unsigned char  ui_u8;
typedef unsigned short ui_u16;

/* ---- screen: every 2D coordinate is virtual 640x448 ---- */
#define UI_SCREEN_W            640.0f
#define UI_SCREEN_H            448.0f

/* ---- CFont: all-int or all-float signatures, safe to call through these ---- */
#define CFONT_SETSCALE         0x002A8890u /* (float w, float h) */
#define CFONT_SETSCALE_LANG    0x002A88B0u /* (float w, float h) w*0.8 for languages 1-4 */
#define CFONT_SETSLANTREFPOINT 0x002A8960u /* (float x, float y) */
#define CFONT_SETSLANT         0x002A8980u /* (float) */
#define CFONT_SETCOLOR         0x002A8990u /* (const ui_rgba*) pointer, not by value */
#define CFONT_SETFONTSTYLE     0x002A8A80u /* (int) see UI_FONT_* */
#define CFONT_SETWRAPX         0x002A8AF0u /* (float) */
#define CFONT_SETCENTRESIZE    0x002A8B00u /* (float) */
#define CFONT_SETRIGHTWRAP     0x002A8B10u /* (float) */
#define CFONT_SETALPHAFADE     0x002A8B20u /* (float) 255 = off */
#define CFONT_SETDROPCOLOR     0x002A8B30u /* (const ui_rgba*) */
#define CFONT_SETDROPSHADOW    0x002A8C20u /* (int px) clears edge */
#define CFONT_SETEDGE          0x002A8C40u /* (int px) clears drop shadow */
#define CFONT_SETPROPORTIONAL  0x002A8C60u /* (int) */
#define CFONT_SETBACKGROUND    0x002A8C70u /* (int on, int framedBox) one-shot, see notes */
#define CFONT_SETBGCOLOR       0x002A8C90u /* (const ui_rgba*) */
#define CFONT_SETJUSTIFY       0x002A8CD0u /* (int) */
#define CFONT_SETORIENTATION   0x002A8CE0u /* (int) see UI_ALIGN_* */
#define CFONT_GETSTRINGWIDTH   0x002A7930u /* float (const char*, int full, int script) */
#define CFONT_DRAWFONTS        0x002A86B0u /* (void) tail-jumps to RENDERFONTBUFFER */
#define CFONT_RENDERFONTBUFFER 0x002A6010u /* (void) */
#define CFONT_INITPERFRAME     0x002A5970u /* (void) game calls it, do not call from UI */
/* mixed signatures: text in $a0, x in $f12, y in $f13. Use the ui_* inlines. */
#define CFONT_PRINTSTRING      0x002A6EB0u
#define CFONT_GETNUMBERLINES   0x002A7060u
#define CFONT_GETTEXTRECT      0x002A77B0u /* rect in $a0, text in $a1 */

#define UI_FONT_GOTHIC         0 /* texture slot 0, lowercase glyphs */
#define UI_FONT_SUBTITLES      1 /* texture slot 1, lowercase glyphs, the subtitle font */
#define UI_FONT_MENU           2 /* slot 0 remapped: lowercase folds to capitals */
#define UI_FONT_PRICEDOWN      3 /* slot 1 remapped: lowercase folds to capitals */

#define UI_ALIGN_CENTRE        0
#define UI_ALIGN_LEFT          1
#define UI_ALIGN_RIGHT         2

/* ---- CFont state block. Queued lines copy what they need, so this can be saved on
 *      hook entry and restored on exit without touching text already queued. ---- */
#define CFONT_STATE            0x007C2390u
#define CFONT_STATE_SIZE       0x40u
#define CFONT_BUFFER           0x007C25F0u /* 512 bytes, ends 0x7C27F0; 0x30 header per line */
#define CFONT_BUFFER_SIZE      0x200u
#define CFONT_LINE_HEIGHT(sy)  (18.0f * (sy)) /* (32*sy)/2 + 2*sy, from 0x2A74C0 */

/* ---- text helpers. Text is 8-bit; AsciiToGxtChar is a plain byte copy. ---- */
#define ASCII_TO_GXT           0x002A5470u /* (const char *src, char *dst) */
#define GXT_STRCPY             0x002A5520u /* char* (char *dst, const char *src) */
#define GXT_STRCAT             0x002A54B0u /* char* (char *dst, const char *src) */
#define GXT_STRLEN             0x002A5560u /* int (const char*) */
#define GXT_TOUPPER            0x002A55A0u /* (char*) in place */
#define CFONT_FILTERTOKENS     0x002A85F0u /* (char*) in place, 256-byte stack copy inside */
#define THETEXT                0x0069F310u
#define CTEXT_GET              0x0018ED90u /* char* (CText*, const char *key) */

/* ---- CSprite2d. Rect is {left, y_bottom, right, y_top}: +4 is the larger y. ---- */
#define CSPRITE2D_DRAWRECT     0x002B2120u /* (const ui_rect*, const ui_rgba*) */
#define CSPRITE2D_DRAWRECT4    0x002B21F0u /* (const ui_rect*, c0,c1,c2,c3) per-corner colour */
#define CSPRITE2D_INITPERFRAME 0x002B0920u /* (void) the jal our 0x246770 hook displaces */

/* ---- RenderWare engine pointers, filled at runtime: read the word, call through it ---- */
#define RW_RENDERSTATESET_PTR  0x00825050u /* int (int state, int value) */
#define RW_IM2DRENDERPRIM_PTR  0x00825060u /* int (int prim, void *verts, int n) */
#define RW_STATE_TEXTURERASTER 1
#define RW_STATE_TEXTUREADDRESS 2
#define RW_STATE_ZTESTENABLE   6
#define RW_STATE_SHADEMODE     7
#define RW_STATE_ZWRITEENABLE  8
#define RW_STATE_TEXTUREFILTER 9
#define RW_STATE_SRCBLEND      10
#define RW_STATE_DESTBLEND     11
#define RW_STATE_VERTEXALPHA   12
#define RW_STATE_FOGENABLE     14
#define RW_BLEND_SRCALPHA      5
#define RW_BLEND_INVSRCALPHA   6

/* ---- frame structure and hook sites, all resident ---- */
#define RENDER2DSTUFF          0x00246FF0u
#define RENDER2DAFTERFADE      0x00247300u
#define CHUD_DRAW              0x002B0030u
#define CHUD_DRAWAFTERFADE     0x002B02C0u
#define HOOK_DRAW_SITE         0x00247318u /* jal CFONT_DRAWFONTS, original word below */
#define HOOK_DRAW_ORIG         0x0C0AA1ACu
#define HOOK_PAD_SITE          0x0024338Cu /* jal CPAD_UPDATEPADS, delay slot is sq s0,0(sp) */
#define HOOK_PAD_ORIG          0x0C091E74u
#define MENU_MANAGER           0x006FF9C0u
#define MENU_ACTIVE            0x006FFA28u /* byte, FrontEnd menu open */

/* ---- CPad ---- */
#define CPAD_ARRAY             0x00700BC0u
#define CPAD_STRIDE            0x94u
#define CPAD_GETPAD            0x00248F00u /* CPad* (int) */
#define CPAD_UPDATEPADS        0x002479D0u /* (void) first call in CGame::Process */
#define CPAD_CLEAR             0x00247640u /* (CPad*, int enableControls, int resetPhase) */

#define CPAD_NEWSTATE          0x00u
#define CPAD_OLDSTATE          0x28u
#define CPAD_STATE_SIZE        0x28u
#define CPAD_MODE              0x6Au /* u16 controller config, 0-3 */
#define CPAD_DISABLECONTROLS   0x6Eu /* u16 bitmask, non-zero blocks most getters */
#define CPAD_JUSTOUTOFFRONTEND 0x77u /* u8 counter, decremented per update */
#define CPAD_LASTTOUCHED       0x80u /* u32 ms */
/* Bit 0x8000 has no game user. Used bits seen: 1,4,8,0x20,0x80,0x100. */
#define CPAD_DISABLE_UI_BIT    0x8000u

typedef struct {
    short lx, ly, rx, ry;       /* about -127..127, 0 inside the deadzone */
    short l1, l2, r1, r2;
    short up, down, left, right;
    short start, select;
    short square, triangle, cross, circle;
    short l3, r3;
} ui_padstate;                  /* 0x28 bytes. Field index == SCM button id. Buttons are
                                   0 when up, non-zero when down: 255, or analog pressure
                                   in pressure mode, so never compare with 255. */

typedef struct { ui_u8 r, g, b, a; } ui_rgba;
typedef struct { float left, y_bottom, right, y_top; } ui_rect;

/* ---- safe typed calls ---- */
typedef void  (*ui_f_ff)(float, float);
typedef void  (*ui_f_f)(float);
typedef void  (*ui_f_i)(int);
typedef void  (*ui_f_ii)(int, int);
typedef void  (*ui_f_v)(void);
typedef void  (*ui_f_rgba)(const ui_rgba *);
typedef float (*ui_f_width)(const char *, int, int);
typedef void  (*ui_f_rect)(const ui_rect *, const ui_rgba *);
typedef void  (*ui_f_rect4)(const ui_rect *, const ui_rgba *, const ui_rgba *,
                            const ui_rgba *, const ui_rgba *);
typedef int   (*ui_f_rwstate)(int, int);
typedef void *(*ui_f_getpad)(int);

#define CFont_SetScale(w, h)        ((ui_f_ff)CFONT_SETSCALE)((w), (h))
#define CFont_SetSlant(s)           ((ui_f_f)CFONT_SETSLANT)((s))
#define CFont_SetColor(c)           ((ui_f_rgba)CFONT_SETCOLOR)((c))
#define CFont_SetFontStyle(s)       ((ui_f_i)CFONT_SETFONTSTYLE)((s))
#define CFont_SetWrapx(x)           ((ui_f_f)CFONT_SETWRAPX)((x))
#define CFont_SetCentreSize(x)      ((ui_f_f)CFONT_SETCENTRESIZE)((x))
#define CFont_SetRightWrap(x)       ((ui_f_f)CFONT_SETRIGHTWRAP)((x))
#define CFont_SetAlphaFade(a)       ((ui_f_f)CFONT_SETALPHAFADE)((a))
#define CFont_SetDropColor(c)       ((ui_f_rgba)CFONT_SETDROPCOLOR)((c))
#define CFont_SetDropShadow(n)      ((ui_f_i)CFONT_SETDROPSHADOW)((n))
#define CFont_SetEdge(n)            ((ui_f_i)CFONT_SETEDGE)((n))
#define CFont_SetProportional(b)    ((ui_f_i)CFONT_SETPROPORTIONAL)((b))
#define CFont_SetBackground(a, b)   ((ui_f_ii)CFONT_SETBACKGROUND)((a), (b))
#define CFont_SetBackgroundColor(c) ((ui_f_rgba)CFONT_SETBGCOLOR)((c))
#define CFont_SetJustify(b)         ((ui_f_i)CFONT_SETJUSTIFY)((b))
#define CFont_SetOrientation(o)     ((ui_f_i)CFONT_SETORIENTATION)((o))
#define CFont_GetStringWidth(s, full, script) \
    ((ui_f_width)CFONT_GETSTRINGWIDTH)((s), (full), (script))
#define CFont_DrawFonts()           ((ui_f_v)CFONT_DRAWFONTS)()
#define CSprite2d_DrawRect(r, c)    ((ui_f_rect)CSPRITE2D_DRAWRECT)((r), (c))
#define RwRenderStateSet(s, v)      ((*(ui_f_rwstate *)RW_RENDERSTATESET_PTR)((s), (v)))
#define CPad_GetPad(n)              ((ui_f_getpad)CPAD_GETPAD)((n))

/* ---- EABI bridges for the mixed signatures: set the exact registers and call ---- */
#define UI_EABI_CLOBBERS "$1", "$3", "$7", "$8", "$9", "$10", "$11", "$12", "$13", "$14", \
    "$15", "$24", "$31", "hi", "lo", "memory", \
    "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8", "$f9", "$f10", \
    "$f11", "$f14", "$f15", "$f16", "$f17", "$f18", "$f19"

/* CFont::PrintString: queues the text (copied, so a transient buffer is fine). */
static inline void ui_PrintString(float x, float y, const char *text)
{
    register const char *a0 __asm__("$4") = text;
    register float f12 __asm__("$f12") = x;
    register float f13 __asm__("$f13") = y;
    register unsigned t9 __asm__("$25") = CFONT_PRINTSTRING;
    register int v0 __asm__("$2");
    register int a1 __asm__("$5");
    register int a2 __asm__("$6");
    __asm__ volatile(".set push\n\t.set noreorder\n\tjalr $25\n\tnop\n\t.set pop"
        : "=r"(v0), "=r"(a1), "=r"(a2), "+r"(a0), "+r"(t9), "+f"(f12), "+f"(f13)
        : : UI_EABI_CLOBBERS);
}

/* CFont::GetNumberLines(x, y, text): lines the string would wrap to. */
static inline int ui_GetNumberLines(float x, float y, const char *text)
{
    register const char *a0 __asm__("$4") = text;
    register float f12 __asm__("$f12") = x;
    register float f13 __asm__("$f13") = y;
    register unsigned t9 __asm__("$25") = CFONT_GETNUMBERLINES;
    register int v0 __asm__("$2");
    register int a1 __asm__("$5");
    register int a2 __asm__("$6");
    __asm__ volatile(".set push\n\t.set noreorder\n\tjalr $25\n\tnop\n\t.set pop"
        : "=r"(v0), "=r"(a1), "=r"(a2), "+r"(a0), "+r"(t9), "+f"(f12), "+f"(f13)
        : : UI_EABI_CLOBBERS);
    return (short)v0;
}

/* CFont::GetTextRect(rect, x, y, text): fills rect for the wrapped string. */
static inline void ui_GetTextRect(ui_rect *rect, float x, float y, const char *text)
{
    register ui_rect *a0 __asm__("$4") = rect;
    register const char *a1 __asm__("$5") = text;
    register float f12 __asm__("$f12") = x;
    register float f13 __asm__("$f13") = y;
    register unsigned t9 __asm__("$25") = CFONT_GETTEXTRECT;
    register int v0 __asm__("$2");
    register int a2 __asm__("$6");
    __asm__ volatile(".set push\n\t.set noreorder\n\tjalr $25\n\tnop\n\t.set pop"
        : "=r"(v0), "=r"(a2), "+r"(a0), "+r"(a1), "+r"(t9), "+f"(f12), "+f"(f13)
        : : UI_EABI_CLOBBERS);
}

#endif
