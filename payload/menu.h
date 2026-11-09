#pragma once
/* Server browser and text entry. Input arrives from the pad hook; drawing happens in
 * the render hook. menu_tick runs once per frame. */
typedef struct {
    short lx, ly, rx, ry;
    short l1, l2, r1, r2;
    short up, down, left, right;
    short start, select;
    short square, triangle, cross, circle;
    short l3, r3;
} menu_pad_t;

/* Provided by main.c. */
void menu_do_connect(unsigned ip, unsigned port, const char *name, const char *password);
void menu_do_disconnect(void);
void menu_do_chat(const char *text);
int  menu_is_connected(void);

void menu_init(void);
void menu_open(void);
int  menu_is_open(void);   /* true while the game must not see the pad */
void menu_pad(const menu_pad_t *pad);
void menu_tick(unsigned now);
void menu_draw(void);
void menu_set_status(const char *text);
/* Opens the text box for someone else. done runs once, from menu_tick: accepted is 1
 * with the text, which may be empty and is only valid during the call, or 0 and "".
 * menu_is_open() is true until then. masked hides what is typed. */
void menu_text_open(const char *prompt, int max_len, int masked,
                    void (*done)(int accepted, const char *text));
