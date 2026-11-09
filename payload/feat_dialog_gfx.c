/* The feat_dialog modules' only contact with the game. See feat_dialog_gfx.h. */
#include "feat_dialog_gfx.h"
#include "feat_dialog_text.h"
#include "ui_game.h"
#include "scm.h"

#define OP_ADD_BLIP_FOR_CHAR  0x0187
#define OP_CHANGE_BLIP_DISPLAY 0x018B
#define BLIP_RADAR_ONLY 2
#define OP_ADD_BLIP_FOR_COORD 0x018A
#define OP_REMOVE_BLIP        0x0164
#define OP_CHANGE_BLIP_COLOUR 0x0165
#define OP_CAMERA_POS         0x068D
#define OP_CAMERA_POINT_AT    0x068E
#define OP_CAMERA_FOV         0x0801

#define L_BLIP 8 /* locals 0 and 1 are game.c's for good, 2 and 3 are scratch for others */
#define L_CAM  9 /* 9-15: position, point looked at, fov */

#define NO_WRAP 8000.0f

static ui_rgba rgba(unsigned c)
{
    ui_rgba r;
    r.r = c;
    r.g = c >> 8;
    r.b = c >> 16;
    r.a = c >> 24;
    return r;
}

void fd_font(int font, float sx, float sy, int proportional, int edge, int shadow, unsigned drop)
{
    ui_rgba d = rgba(drop);

    CFont_SetFontStyle(font);
    CFont_SetScale(sx, sy);
    CFont_SetOrientation(UI_ALIGN_LEFT);
    CFont_SetProportional(proportional != 0);
    CFont_SetJustify(0);
    CFont_SetSlant(0.0f);
    CFont_SetWrapx(NO_WRAP);
    CFont_SetRightWrap(0.0f);
    CFont_SetCentreSize(NO_WRAP);
    CFont_SetAlphaFade(255.0f);
    CFont_SetDropColor(&d);
    if (edge)
        CFont_SetEdge(edge);
    else
        CFont_SetDropShadow(shadow);
    CFont_SetBackground(0, 0);
}

float fd_width(const char *s) { return CFont_GetStringWidth(s, 1, 0); }

void fd_text(float x, float y, const char *s, unsigned color)
{
    ui_rgba c = rgba(color);
    char b[FD_RUN_MAX + 2];
    unsigned n = fd_copy(b + 1, s, sizeof b - 1);

    /* The game draws nothing at all for a string that ends in a space. */
    while (n && b[n] == ' ')
        b[n--] = 0;
    if (!n)
        return;
    CFont_SetColor(&c);
    if (b[1] == '*') { /* the game skips a whole string starting with '*' */
        b[0] = ' ';
        ui_PrintString(x - fd_width(" "), y, b);
        return;
    }
    ui_PrintString(x, y, b + 1);
}

void fd_rect(float l, float t, float r, float b, unsigned color)
{
    ui_rgba c = rgba(color);
    ui_rect q;

    q.left = l;
    q.right = r;
    q.y_top = t;
    q.y_bottom = b;
    CSprite2d_DrawRect(&q, &c);
}

void fd_flush(void) { CFont_DrawFonts(); }

int fd_blip_for_char(int char_handle)
{
    scm_t s;

    scm_set_local(L_BLIP, char_handle);
    scm_begin(&s);
    scm_op(&s, OP_ADD_BLIP_FOR_CHAR); scm_local(&s, L_BLIP); scm_local(&s, L_BLIP);
    /* Radar only: the default also floats a big arrow over the ped. */
    scm_op(&s, OP_CHANGE_BLIP_DISPLAY); scm_local(&s, L_BLIP); scm_i8(&s, BLIP_RADAR_ONLY);
    scm_run(&s);
    return scm_get_local(L_BLIP);
}

int fd_blip_for_coord(const float *pos)
{
    scm_t s;

    scm_begin(&s);
    scm_op(&s, OP_ADD_BLIP_FOR_COORD);
    scm_f(&s, pos[0]); scm_f(&s, pos[1]); scm_f(&s, pos[2]);
    scm_local(&s, L_BLIP);
    scm_op(&s, OP_CHANGE_BLIP_DISPLAY); scm_local(&s, L_BLIP); scm_i8(&s, BLIP_RADAR_ONLY);
    scm_run(&s);
    return scm_get_local(L_BLIP);
}

void fd_blip_colour(int blip, unsigned rgba_color)
{
    scm_t s;

    scm_set_local(L_BLIP, blip);
    scm_begin(&s);
    scm_op(&s, OP_CHANGE_BLIP_COLOUR); scm_local(&s, L_BLIP); scm_i32(&s, (int)rgba_color);
    scm_run(&s);
}

void fd_blip_remove(int blip)
{
    scm_t s;

    scm_set_local(L_BLIP, blip);
    scm_begin(&s);
    scm_op(&s, OP_REMOVE_BLIP); scm_local(&s, L_BLIP);
    scm_run(&s);
}

int fd_paused(void) { return *(volatile unsigned char *)MENU_ACTIVE != 0; }

static float local_f(int i)
{
    union { int i; float f; } u;
    u.i = scm_get_local(i);
    return u.f;
}

void fd_camera_update(void)
{
    float pos[3], at[3];
    scm_t s;
    int i;

    scm_begin(&s);
    scm_op(&s, OP_CAMERA_POS);
    scm_local(&s, L_CAM); scm_local(&s, L_CAM + 1); scm_local(&s, L_CAM + 2);
    scm_op(&s, OP_CAMERA_POINT_AT);
    scm_local(&s, L_CAM + 3); scm_local(&s, L_CAM + 4); scm_local(&s, L_CAM + 5);
    scm_op(&s, OP_CAMERA_FOV);
    scm_local(&s, L_CAM + 6);
    scm_run(&s);
    for (i = 0; i < 3; i++) {
        pos[i] = local_f(L_CAM + i);
        at[i] = local_f(L_CAM + 3 + i);
    }
    fd_camera_set(pos, at, local_f(L_CAM + 6));
}
