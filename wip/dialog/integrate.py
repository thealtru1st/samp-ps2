#!/usr/bin/env python3
"""The Integration list from the report, as code: integrate.py <payload dir> <out dir>.
Writes patched copies of samp.c, main.c, menu.c, menu.h, game.c and game.h to out dir
and leaves the originals alone. Every edit must apply exactly once or it stops, so it
also says when one of those files has moved on from what this was written against."""
import os
import sys

src_dir, out_dir = sys.argv[1], sys.argv[2]


def patch(name, edits):
    text = open(os.path.join(src_dir, name)).read()  # universal newlines: these are CRLF
    for edit in edits:
        old, new = edit[:2]
        want = edit[2] if len(edit) > 2 else 1
        assert text.count(old) == want, '%s: %r matched %d times' % (name, old, text.count(old))
        text = text.replace(old, new)
    open(os.path.join(out_dir, name), 'w').write(text)


# 1. Feature handlers see every RPC, not just the ones samp.c has no case for.
patch('samp.c', [
    ('static void on_rpc(unsigned id, bs_t *b)\n{\n    switch (id) {',
     'static void on_rpc(unsigned id, bs_t *b)\n{\n'
     '    bs_t copy = *b;\n    samp_rpc_dispatch(id, &copy);\n    switch (id) {'),
    ('    default:\n        samp_rpc_dispatch(id, b);\n        break;\n', ''),
])

# 2. The pad reaches dialogs, and the game does not see it while one is up.
patch('main.c', [
    ('#include "feat.h"\n', '#include "feat.h"\n#include "feat_dialog_pad.h"\n'),
    ('    menu_pad(&p);\n', '    menu_pad(&p);\n    feat_dialog_pad(&p);\n'),
    ('    if (menu_is_open()) {\n        release_wait = 1;',
     '    if (menu_is_open() || feat_dialog_modal()) {\n        release_wait = 1;'),
])

# 3. The shared text box, with a result callback.
patch('menu.h', [
    ('void menu_set_status(const char *text);\n',
     'void menu_set_status(const char *text);\n'
     '/* Opens the text box for someone else. done runs once, from menu_tick: accepted is 1\n'
     ' * with the text, which may be empty and is only valid during the call, or 0 and "".\n'
     ' * menu_is_open() is true until then. masked hides what is typed. */\n'
     'void menu_text_open(const char *prompt, int max_len, int masked,\n'
     '                    void (*done)(int accepted, const char *text));\n'),
])
patch('menu.c', [
    ('enum { EDIT_ADDR, EDIT_NAME, EDIT_PASSWORD, EDIT_CHAT };',
     'enum { EDIT_ADDR, EDIT_NAME, EDIT_PASSWORD, EDIT_CHAT, EDIT_CALLBACK };'),
    ('static unsigned kb_seen;\n',
     'static unsigned kb_seen;\n'
     'static void (*edit_cb)(int accepted, const char *text);\n'
     'static int edit_masked;\n'),
    ('static void cycle(int dir)\n',
     'void menu_text_open(const char *prompt, int max_len, int masked,\n'
     '                    void (*done)(int accepted, const char *text))\n'
     '{\n'
     '    if (max_len > (int)sizeof edit_buf - 1)\n'
     '        max_len = sizeof edit_buf - 1;\n'
     '    edit_cb = done;\n'
     '    edit_masked = masked;\n'
     '    edit_start(EDIT_CALLBACK, prompt, "", max_len);\n'
     '}\n\n'
     'static void cycle(int dir)\n'),
    ('static void edit_done(void)\n{\n    if (edit_mode == EDIT_ADDR) {',
     'static void edit_done(void)\n{\n'
     '    if (edit_mode == EDIT_CALLBACK) {\n'
     '        view = VIEW_HIDDEN;\n'
     '        edit_masked = 0;\n'
     '        edit_cb(1, edit_buf);\n'
     '    } else if (edit_mode == EDIT_ADDR) {'),
    ('static void edit_close(int next_view)\n{\n    ST->edit_open = 0;\n    view = next_view;\n}\n',
     'static void edit_close(int next_view)\n{\n    ST->edit_open = 0;\n    view = next_view;\n}\n\n'
     'static void edit_cancel(void)\n'
     '{\n'
     '    if (edit_mode == EDIT_CALLBACK) {\n'
     '        edit_close(VIEW_HIDDEN);\n'
     '        edit_masked = 0;\n'
     '        edit_cb(0, "");\n'
     '    } else {\n'
     '        edit_close(edit_mode == EDIT_CHAT ? VIEW_HIDDEN : VIEW_LIST);\n'
     '    }\n'
     '}\n'),
    ('        ST->edit_open = 0;\n        if (edit_buf[0])\n            edit_done();',
     '        ST->edit_open = 0;\n        if (edit_buf[0] || edit_mode == EDIT_CALLBACK)\n            edit_done();'),
    ('    } else if (PRESSED(circle)) {\n'
     '        edit_close(edit_mode == EDIT_CHAT ? VIEW_HIDDEN : VIEW_LIST);\n'
     '    }\n'
     '}\n\n'
     'static void edit_input(void)\n',
     '    } else if (PRESSED(circle)) {\n'
     '        edit_cancel();\n'
     '    }\n'
     '}\n\n'
     'static void edit_input(void)\n'),
    ('        ST->edit_open = 0;\n        edit_done();\n'
     '    } else if (PRESSED(circle)) {\n'
     '        edit_close(edit_mode == EDIT_CHAT ? VIEW_HIDDEN : VIEW_LIST);\n'
     '    }\n',
     '        ST->edit_open = 0;\n        edit_done();\n'
     '    } else if (PRESSED(circle)) {\n'
     '        edit_cancel();\n'
     '    }\n'),
    ('    if (edit_mode == EDIT_PASSWORD) {\n        int i;\n        for (i = 0; before[i]; i++)',
     '    if (edit_mode == EDIT_PASSWORD || edit_masked) {\n        int i;\n        for (i = 0; before[i]; i++)',
     2),
])

# 4. Where remote players are, for name tags, bubbles and markers.
patch('game.h', [
    ('void game_remote_clear(void);\n',
     'void game_remote_clear(void);\n'
     'int  game_remote_pos(unsigned id, float *pos); /* 0 if not streamed in */\n'
     'int  game_remote_handle(unsigned id);          /* script handle, 0 if not streamed in */\n'),
])
patch('game.c', [
    ('void game_remote_clear(void)\n',
     'int game_remote_pos(unsigned id, float *pos)\n'
     '{\n'
     '    remote_t *r = find_remote(id);\n'
     '    if (!r)\n'
     '        return 0;\n'
     '    pos[0] = r->tx;\n'
     '    pos[1] = r->ty;\n'
     '    pos[2] = r->tz;\n'
     '    return 1;\n'
     '}\n\n'
     'int game_remote_handle(unsigned id)\n'
     '{\n'
     '    remote_t *r = find_remote(id);\n'
     '    return r ? r->handle : 0;\n'
     '}\n\n'
     'void game_remote_clear(void)\n'),
])
print('patched copies in', out_dir)
