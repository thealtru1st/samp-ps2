/* On-screen UI, drawn with the game's own font and rect code. Coordinates are the
 * game's virtual 640x448 space. */
#include "ui.h"
#include "menu.h"
#include "feat.h"
#include "ui_game.h"
#include "ui_font.h"

int fd_paused(void);
#include "util.h"

#define CHAT_LINES 8
#define CHAT_LEN   120
#define LINE_H     13.0f

static char chat[CHAT_LINES][CHAT_LEN];
static int chat_head, chat_count;
static char status_text[64];

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Removes SA-MP {RRGGBB} colour tags, and characters the game's font treats as
 * control codes: '~' starts a token, a leading '*' hides the whole string and '_' has no
 * glyph. See ui_font_char for the rest. */
static void clean(char *out, const char *in, unsigned max)
{
    unsigned n = 0;
    while (*in && n < max - 1) {
        if (in[0] == '{' && strlen(in) >= 8 && in[7] == '}') {
            int i, ok = 1;
            for (i = 1; i < 7; i++)
                if (!is_hex(in[i]))
                    ok = 0;
            if (ok) {
                in += 8;
                continue;
            }
        }
        char c = *in++;
        if (c == '~' || c == '_')
            c = ' ';
        if ((unsigned char)c < 32)
            c = ' ';
        c = ui_font_char(c);
        if (c == '*' && n == 0) { /* '#' at the start: a leading '*' would hide the line */
            if (n + 2 >= max)
                break;
            out[n++] = ' ';
        }
        out[n++] = c;
    }
    out[n] = 0;
}

void ui_chat_add(const char *text)
{
    clean(chat[chat_head], text, CHAT_LEN);
    chat_head = (chat_head + 1) % CHAT_LINES;
    if (chat_count < CHAT_LINES)
        chat_count++;
}

void ui_status(const char *text)
{
    clean(status_text, text, sizeof status_text);
}

static void text_style(void)
{
    static const ui_rgba white = {255, 255, 255, 255}, black = {0, 0, 0, 255};
    CFont_SetFontStyle(UI_FONT_SUBTITLES);
    CFont_SetScale(0.38f, 0.72f);
    CFont_SetOrientation(UI_ALIGN_LEFT);
    CFont_SetProportional(1);
    CFont_SetJustify(0);
    CFont_SetSlant(0.0f);
    CFont_SetWrapx(620.0f);
    CFont_SetRightWrap(620.0f);
    CFont_SetAlphaFade(255.0f);
    CFont_SetColor(&white);
    CFont_SetDropColor(&black);
    CFont_SetEdge(1);
    CFont_SetBackground(0, 0);
}

void ui_draw(void)
{
    static unsigned char saved[CFONT_STATE_SIZE];
    int i;
    float y = 24.0f;

    memcpy(saved, (void *)CFONT_STATE, CFONT_STATE_SIZE);
    CFont_DrawFonts(); /* flush game text first so ours stacks above it */
    RwRenderStateSet(RW_STATE_ZTESTENABLE, 0);
    RwRenderStateSet(RW_STATE_ZWRITEENABLE, 0);
    RwRenderStateSet(RW_STATE_SRCBLEND, RW_BLEND_SRCALPHA);
    RwRenderStateSet(RW_STATE_DESTBLEND, RW_BLEND_INVSRCALPHA);
    RwRenderStateSet(RW_STATE_FOGENABLE, 0);
    RwRenderStateSet(RW_STATE_VERTEXALPHA, 1);
    text_style();
    for (i = 0; i < chat_count && !fd_paused(); i++) {
        int idx = (chat_head - chat_count + i + CHAT_LINES) % CHAT_LINES;
        if (chat[idx][0]) {
            ui_PrintString(16.0f, y, chat[idx]);
            /* The game wraps a long line by itself, so leave room for what it adds. */
            y += CFont_GetStringWidth(chat[idx], 1, 0) > 600.0f ? 2 * LINE_H : LINE_H;
        }
    }
    if (status_text[0])
        ui_PrintString(16.0f, 8.0f, status_text);
    CFont_DrawFonts();
    feat_draw();
    CFont_DrawFonts();
    menu_draw();
    CFont_DrawFonts();
    memcpy((void *)CFONT_STATE, saved, CFONT_STATE_SIZE);
}
