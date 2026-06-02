#ifndef MEMEX_UI_CURSES_H
#define MEMEX_UI_CURSES_H

#include <curses.h>
#include <string.h>

#if defined(MEMEX_DISABLE_MOUSE)
#define MEMEX_HAS_CURSES_MOUSE 0
#elif defined(KEY_MOUSE)
#define MEMEX_HAS_CURSES_MOUSE 1
#else
#define MEMEX_HAS_CURSES_MOUSE 0
#endif

#ifdef KEY_BTAB
#define MEMEX_HAS_KEY_BTAB 1
#else
#define MEMEX_HAS_KEY_BTAB 0
#endif

enum {
    MEMEX_KEY_NONE = 0,
    MEMEX_KEY_UP = 1000,
    MEMEX_KEY_DOWN,
    MEMEX_KEY_LEFT,
    MEMEX_KEY_RIGHT,
    MEMEX_KEY_HOME,
    MEMEX_KEY_END,
    MEMEX_KEY_PPAGE,
    MEMEX_KEY_NPAGE,
    MEMEX_KEY_ENTER,
    MEMEX_KEY_BACKSPACE,
    MEMEX_KEY_TAB,
    MEMEX_KEY_BTAB,
    MEMEX_KEY_ESCAPE,
    MEMEX_KEY_MOUSE
};

static int ui_key_normalize(int ch)
{
    switch (ch) {
    case '\n':
    case '\r':
    case KEY_ENTER:
        return MEMEX_KEY_ENTER;
    case KEY_BACKSPACE:
    case 127:
    case 8:
        return MEMEX_KEY_BACKSPACE;
    case '\t':
        return MEMEX_KEY_TAB;
#if MEMEX_HAS_KEY_BTAB
    case KEY_BTAB:
        return MEMEX_KEY_BTAB;
#endif
    case 27:
        return MEMEX_KEY_ESCAPE;
    case KEY_UP:
        return MEMEX_KEY_UP;
    case KEY_DOWN:
        return MEMEX_KEY_DOWN;
    case KEY_LEFT:
        return MEMEX_KEY_LEFT;
    case KEY_RIGHT:
        return MEMEX_KEY_RIGHT;
    case KEY_HOME:
        return MEMEX_KEY_HOME;
    case KEY_END:
        return MEMEX_KEY_END;
    case KEY_NPAGE:
        return MEMEX_KEY_NPAGE;
    case KEY_PPAGE:
        return MEMEX_KEY_PPAGE;
#if MEMEX_HAS_CURSES_MOUSE
    case KEY_MOUSE:
        return MEMEX_KEY_MOUSE;
#endif
    default:
        return ch;
    }
}

static int ui_key_is_enter(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_ENTER;
}

static int ui_key_is_backspace(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_BACKSPACE;
}

static int ui_key_is_escape(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_ESCAPE;
}

static int ui_key_is_tab(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_TAB;
}

static int ui_key_is_backtab(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_BTAB;
}

#if MEMEX_HAS_CURSES_MOUSE
static int ui_key_is_mouse(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_MOUSE;
}
#endif

static int ui_key_is_up(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_UP;
}

static int ui_key_is_down(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_DOWN;
}

static int ui_key_is_left(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_LEFT;
}

static int ui_key_is_right(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_RIGHT;
}

static int ui_key_is_page_down(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_NPAGE;
}

static int ui_key_is_page_up(int ch)
{
    return ui_key_normalize(ch) == MEMEX_KEY_PPAGE;
}

static int ui_start_theme_color(const char *theme_name)
{
    short fg = COLOR_WHITE;
    short bg = COLOR_BLUE;

    if (!has_colors())
        return 0;
    if (theme_name && strcmp(theme_name, "amber") == 0) {
        fg = COLOR_YELLOW;
        bg = COLOR_BLACK;
    } else if (theme_name && strcmp(theme_name, "forest") == 0) {
        fg = COLOR_GREEN;
        bg = COLOR_BLACK;
    } else if (theme_name && strcmp(theme_name, "ocean") == 0) {
        fg = COLOR_CYAN;
        bg = COLOR_BLACK;
    }
    start_color();
    init_pair(1, fg, bg);
    bkgd(COLOR_PAIR(1));
    return 1;
}

static void ui_init_keyboard(void)
{
    keypad(stdscr, TRUE);
}

static int ui_read_key(void)
{
    return ui_key_normalize(getch());
}

#endif
