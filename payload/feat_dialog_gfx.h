#pragma once
/* Drawing and platform layer for the feat_dialog modules. Everything that touches the
 * game lives behind this header, in feat_dialog_gfx.c, so the logic above it builds and
 * runs on the host against wip/dialog/fake_gfx.c.
 *
 * Colours are 0xAABBGGRR: the byte order of the game's CRGBA, and of the ABGR words on
 * the wire. Text is printed one run at a time, left aligned with the game's wrapping
 * turned off. Layout is ours (feat_dialog_text.c) because the game's is not safe for
 * server text: see that file. */

#define FD_RGBA(r, g, b, a) ((unsigned)(r) | (unsigned)(g) << 8 | (unsigned)(b) << 16 | (unsigned)(a) << 24)
#define FD_RGB(r, g, b)     FD_RGBA(r, g, b, 255)
/* A server's RGBA word (red in the top byte) to this order. */
#define FD_FROM_RGBA(c)     (((c) >> 24 & 255) | ((c) >> 16 & 255) << 8 | ((c) >> 8 & 255) << 16 | ((c) & 255) << 24)

enum { FD_FONT_GOTHIC, FD_FONT_SUBTITLES, FD_FONT_MENU, FD_FONT_PRICEDOWN };

/* Sets every font state the game leaves stale. edge wins over shadow. */
void  fd_font(int font, float sx, float sy, int proportional, int edge, int shadow, unsigned drop);
float fd_width(const char *s);                 /* in the current font */
void  fd_text(float x, float y, const char *s, unsigned color); /* s at most FD_RUN_MAX chars */
void  fd_rect(float l, float t, float r, float b, unsigned color);
void  fd_flush(void);                          /* draw queued text now */

/* Blips for remote players. Handles are the game's; 0 means none. */
int   fd_blip_for_char(int char_handle);
int   fd_blip_for_coord(const float *pos);
void  fd_blip_colour(int blip, unsigned rgba);
void  fd_blip_remove(int blip);

/* World to 640x448 screen. Returns 0 behind the camera or when unavailable; *dist is
 * the distance from the camera in metres. */
int   fd_project(const float *world, float *x, float *y, float *dist);
/* Once per frame from tick, when anything will need fd_project. */
void  fd_camera_update(void);
/* What fd_camera_update reads from the game, into fd_project. */
void  fd_camera_set(const float *pos, const float *point_at, float fov);

/* The game's own pause menu is up: it draws over nothing of ours and needs the pad. */
int   fd_paused(void);
