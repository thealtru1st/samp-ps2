#pragma once
#define FAKE_MAX 4096
typedef struct { float x, y; unsigned color; char s[256]; } fake_text_t;
typedef struct { float l, t, r, b; unsigned color; } fake_rect_t;
extern fake_text_t fake_texts[FAKE_MAX];
extern fake_rect_t fake_rects[FAKE_MAX];
extern int fake_ntext, fake_nrect, fake_nblip, fake_project_ok, fake_paused;
void fake_clear(void);
int  fake_find(const char *needle); /* index of the first text containing it, or -1 */
void fake_dump(void);
