/* The build as it stands until menu_text_open exists: the weak symbol is null, and an
 * input dialog must still be answerable. */
#include <stdio.h>
#include <string.h>
#include "feat_dialog_int.h"
#include "feat_dialog_pad.h"
#include "samp.h"
#include "rak.h"
#include "fake_gfx.h"

samp_stats_t samp_stats;
int samp_state(void) { return SAMP_SPAWNED; }
int menu_is_open(void) { return 0; }
int menu_is_connected(void) { return 1; }
void slog(const char *f, ...) {}
static int got_id, got_bits;
int rak_rpc(int id, const void *d, int bits, int rel, int chan) { got_id = id; got_bits = bits; return 0; }

int main(void)
{
    static samp_rpc_handlers_t h;
    static srpc_dialog_t d;
    menu_pad_t press, idle;
    unsigned now = 1000;
    int i;

    feat_dialog_init(&h);
    memset(&idle, 0, sizeof idle);
    press = idle;
    press.cross = 255;
    d.id = 5; d.style = 3;
    strcpy(d.title, "Login"); strcpy(d.button1, "Go"); strcpy(d.body, "password");
    h.show_dialog(&d);
    for (i = 0; i < 30; i++) {
        feat_dialog_pad(&idle);
        fake_clear();
        feat_dialog_tick(now += 16);
        feat_dialog_draw();
    }
    if (fake_find("No text entry") < 0) { puts("FAIL no notice"); return 1; }
    feat_dialog_pad(&press);
    feat_dialog_tick(now += 16);
    if (got_id != 62 || feat_dialog_modal()) { puts("FAIL still stuck"); return 1; }
    puts("no text box: ok");
    return 0;
}
