#pragma once
/* Printable ASCII to what shows it in the game's font, seen on screen on this binary: '*'
 * draws '#', '#' draws a registered mark, '@' a trademark, '[' and ']' are button icons and
 * '^', '`', '{', '|', '}' are other odd glyphs. '~', '_' and a leading '*' are control codes
 * and are for the caller, which knows where the string starts. */
static inline char ui_font_char(char c)
{
    switch (c) {
    case '#': return '*';
    case '*': return 'x';
    case '@': return 'a';
    case '[': case '{': return '(';
    case ']': case '}': return ')';
    case '^': case '`': return 0x27;
    case '|': return '!';
    }
    return c;
}
