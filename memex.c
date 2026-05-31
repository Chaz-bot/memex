/*
 * memex - small markdown TUI notes for old Linux systems.
 *
 * Design goals:
 * - C89-friendly source.
 * - curses UI, no threads, no external libraries beyond libc/curses.
 * - bounded memory use for 386SX / 8 MB class machines.
 */

#include <ctype.h>
#include <curses.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define MAX_NOTES 512
#define MAX_NAME 80
#define MAX_TITLE 96
#define MAX_FILTER 80
#define MAX_LINE 240
#define MAX_LINES 2048
#define MAX_NOTE_BYTES (48 * 1024)
#define MAX_LINKS 96
#define MAX_STATUS 160

#define CTRL_KEY(x) ((x) & 037)

typedef struct {
    char title[MAX_TITLE];
    char file[MAX_NAME + 4];
} Note;

typedef struct {
    char label[MAX_TITLE];
    int line;
} Link;

static char note_dir[PATH_MAX];
static Note notes[MAX_NOTES];
static int note_count = 0;
static int selected_note = 0;
static int top_note = 0;
static int current_note = -1;
static int note_scroll = 0;
static char note_filter[MAX_FILTER];
static char status_msg[MAX_STATUS];
static char **view_lines = NULL;
static int view_line_count = 0;
static Link links[MAX_LINKS];
static int link_count = 0;
static int selected_link = 0;
static int show_backlinks = 0;
static int running = 1;

static char edit_lines[MAX_LINES][MAX_LINE + 1];
static int edit_line_count = 0;
static int edit_y = 0;
static int edit_x = 0;
static int edit_scroll = 0;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    while (dst_size > 1 && *src) {
        *dst++ = *src++;
        dst_size--;
    }
    *dst = '\0';
}

static void append_string(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(dst);

    if (len >= dst_size)
        return;
    copy_string(dst + len, dst_size - len, src);
}

static void set_status(const char *msg)
{
    copy_string(status_msg, sizeof(status_msg), msg);
}

static int has_md_suffix(const char *name)
{
    size_t n = strlen(name);
    return n > 3 && strcmp(name + n - 3, ".md") == 0;
}

static int case_contains(const char *s, const char *needle)
{
    char a, b;
    int i, j;

    if (needle[0] == '\0')
        return 1;
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j]; j++) {
            a = (char)tolower((unsigned char)s[i + j]);
            b = (char)tolower((unsigned char)needle[j]);
            if (a != b || s[i + j] == '\0')
                break;
        }
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

static int visible_note_index(int visible_pos)
{
    int i, count = 0;

    for (i = 0; i < note_count; i++) {
        if (case_contains(notes[i].title, note_filter)) {
            if (count == visible_pos)
                return i;
            count++;
        }
    }
    return -1;
}

static int visible_note_count(void)
{
    int i, count = 0;

    for (i = 0; i < note_count; i++) {
        if (case_contains(notes[i].title, note_filter))
            count++;
    }
    return count;
}

static void make_path(char *out, size_t out_size, const char *file)
{
    copy_string(out, out_size, note_dir);
    append_string(out, out_size, "/");
    append_string(out, out_size, file);
}

static void sanitize_title(const char *src, char *title, size_t title_size)
{
    size_t i, o = 0;
    char c;

    while (*src && isspace((unsigned char)*src))
        src++;
    for (i = 0; src[i] && o + 1 < title_size; i++) {
        c = src[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*'
            || c == '?' || c == '"' || c == '<' || c == '>'
            || c == '|') {
            c = '-';
        } else if (iscntrl((unsigned char)c)) {
            continue;
        }
        title[o++] = c;
    }
    while (o > 0 && isspace((unsigned char)title[o - 1]))
        o--;
    title[o] = '\0';
    if (title[0] == '\0')
        copy_string(title, title_size, "Untitled");
}

static void title_to_file(const char *title, char *file, size_t file_size)
{
    char clean[MAX_TITLE];

    sanitize_title(title, clean, sizeof(clean));
    copy_string(file, file_size, clean);
    append_string(file, file_size, ".md");
}

static int note_cmp(const void *a, const void *b)
{
    const Note *na = (const Note *)a;
    const Note *nb = (const Note *)b;
    return strcmp(na->title, nb->title);
}

static void free_view(void)
{
    int i;

    if (!view_lines)
        return;
    for (i = 0; i < view_line_count; i++)
        free(view_lines[i]);
    free(view_lines);
    view_lines = NULL;
    view_line_count = 0;
    link_count = 0;
    selected_link = 0;
}

static char *xstrdup(const char *s)
{
    char *p = (char *)malloc(strlen(s) + 1);
    if (p)
        strcpy(p, s);
    return p;
}

static void scan_links_in_line(const char *line, int line_no)
{
    const char *p = line;
    const char *end;
    int len;

    while ((p = strstr(p, "[[")) != NULL && link_count < MAX_LINKS) {
        p += 2;
        end = strstr(p, "]]");
        if (!end)
            break;
        len = (int)(end - p);
        if (len > 0) {
            if (len >= MAX_TITLE)
                len = MAX_TITLE - 1;
            memcpy(links[link_count].label, p, (size_t)len);
            links[link_count].label[len] = '\0';
            links[link_count].line = line_no;
            link_count++;
        }
        p = end + 2;
    }
}

static void load_note_view(int idx)
{
    char path[PATH_MAX];
    char buf[MAX_LINE + 2];
    FILE *fp;
    long bytes = 0;
    char **new_lines;

    free_view();
    current_note = idx;
    note_scroll = 0;

    if (idx < 0 || idx >= note_count)
        return;

    new_lines = (char **)calloc(MAX_LINES, sizeof(char *));
    if (!new_lines) {
        set_status("Out of memory loading note");
        return;
    }
    view_lines = new_lines;

    make_path(path, sizeof(path), notes[idx].file);
    fp = fopen(path, "r");
    if (!fp) {
        set_status("Could not open note");
        return;
    }

    while (view_line_count < MAX_LINES && fgets(buf, sizeof(buf), fp)) {
        bytes += (long)strlen(buf);
        if (bytes > MAX_NOTE_BYTES) {
            set_status("Note truncated at memory limit");
            break;
        }
        buf[strcspn(buf, "\r\n")] = '\0';
        view_lines[view_line_count] = xstrdup(buf);
        if (!view_lines[view_line_count]) {
            set_status("Out of memory loading note");
            break;
        }
        scan_links_in_line(buf, view_line_count);
        view_line_count++;
    }
    fclose(fp);
}

static void load_notes(void)
{
    DIR *dir;
    struct dirent *ent;
    int keep_current = current_note;
    char current_title[MAX_TITLE];

    current_title[0] = '\0';
    if (keep_current >= 0 && keep_current < note_count)
        copy_string(current_title, sizeof(current_title),
                    notes[keep_current].title);

    note_count = 0;
    dir = opendir(note_dir);
    if (!dir) {
        if (mkdir(note_dir, 0777) != 0 && errno != EEXIST) {
            set_status("Could not create note directory");
            return;
        }
        dir = opendir(note_dir);
        if (!dir) {
            set_status("Could not open note directory");
            return;
        }
    }

    while ((ent = readdir(dir)) != NULL && note_count < MAX_NOTES) {
        if (!has_md_suffix(ent->d_name))
            continue;
        copy_string(notes[note_count].file, sizeof(notes[note_count].file),
                    ent->d_name);
        copy_string(notes[note_count].title, sizeof(notes[note_count].title),
                    ent->d_name);
        notes[note_count].title[strlen(notes[note_count].title) - 3] = '\0';
        note_count++;
    }
    closedir(dir);
    qsort(notes, note_count, sizeof(Note), note_cmp);

    selected_note = 0;
    current_note = -1;
    if (current_title[0]) {
        int i;
        for (i = 0; i < note_count; i++) {
            if (strcmp(current_title, notes[i].title) == 0) {
                current_note = i;
                break;
            }
        }
    }
}

static int prompt_text(const char *prompt, char *out, size_t out_size)
{
    int y = LINES - 2;
    int ch;
    size_t len = strlen(out);

    curs_set(1);
    nodelay(stdscr, FALSE);
    keypad(stdscr, TRUE);

    for (;;) {
        move(y, 0);
        clrtoeol();
        attron(A_REVERSE);
        printw("%s%s", prompt, out);
        attroff(A_REVERSE);
        refresh();
        ch = getch();
        if (ch == 27) {
            curs_set(0);
            return 0;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            curs_set(0);
            return 1;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0)
                out[--len] = '\0';
        } else if (isprint(ch) && len + 1 < out_size) {
            out[len++] = (char)ch;
            out[len] = '\0';
        }
    }
}

static int create_note(const char *title)
{
    char clean[MAX_TITLE];
    char file[MAX_NAME + 4];
    char path[PATH_MAX];
    FILE *fp;

    sanitize_title(title, clean, sizeof(clean));
    title_to_file(clean, file, sizeof(file));
    make_path(path, sizeof(path), file);

    fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        set_status("Note already exists");
        return 0;
    }

    fp = fopen(path, "w");
    if (!fp) {
        set_status("Could not create note");
        return 0;
    }
    fprintf(fp, "# %s\n\n", clean);
    fclose(fp);
    load_notes();
    set_status("Note created");
    return 1;
}

static void delete_current_note(void)
{
    char path[PATH_MAX];

    if (current_note < 0)
        return;
    make_path(path, sizeof(path), notes[current_note].file);
    if (unlink(path) != 0) {
        set_status("Could not delete note");
        return;
    }
    free_view();
    current_note = -1;
    load_notes();
    set_status("Note deleted");
}

static void rename_current_note(void)
{
    char title[MAX_TITLE];
    char file[MAX_NAME + 4];
    char old_path[PATH_MAX], new_path[PATH_MAX];

    if (current_note < 0)
        return;
    copy_string(title, sizeof(title), notes[current_note].title);
    if (!prompt_text("Rename: ", title, sizeof(title)))
        return;
    title_to_file(title, file, sizeof(file));
    make_path(old_path, sizeof(old_path), notes[current_note].file);
    make_path(new_path, sizeof(new_path), file);
    if (rename(old_path, new_path) != 0) {
        set_status("Could not rename note");
        return;
    }
    load_notes();
    set_status("Note renamed");
}

static void load_editor(void)
{
    char path[PATH_MAX];
    char buf[MAX_LINE + 2];
    FILE *fp;
    long bytes = 0;

    edit_line_count = 0;
    edit_y = edit_x = edit_scroll = 0;
    if (current_note < 0)
        return;

    make_path(path, sizeof(path), notes[current_note].file);
    fp = fopen(path, "r");
    if (!fp)
        return;

    while (edit_line_count < MAX_LINES && fgets(buf, sizeof(buf), fp)) {
        bytes += (long)strlen(buf);
        if (bytes > MAX_NOTE_BYTES)
            break;
        buf[strcspn(buf, "\r\n")] = '\0';
        copy_string(edit_lines[edit_line_count],
                    sizeof(edit_lines[edit_line_count]), buf);
        edit_line_count++;
    }
    fclose(fp);
    if (edit_line_count == 0) {
        edit_line_count = 1;
        edit_lines[0][0] = '\0';
    }
}

static int save_editor(void)
{
    char path[PATH_MAX];
    FILE *fp;
    int i;

    if (current_note < 0)
        return 0;
    make_path(path, sizeof(path), notes[current_note].file);
    fp = fopen(path, "w");
    if (!fp) {
        set_status("Could not save note");
        return 0;
    }
    for (i = 0; i < edit_line_count; i++)
        fprintf(fp, "%s\n", edit_lines[i]);
    fclose(fp);
    load_note_view(current_note);
    set_status("Saved");
    return 1;
}

static void draw_header(void)
{
    attron(A_REVERSE);
    move(0, 0);
    clrtoeol();
    printw(" memex  dir:%s  /:%s", note_dir, note_filter[0] ? note_filter : "-");
    attroff(A_REVERSE);
}

static void draw_status(void)
{
    attron(A_REVERSE);
    move(LINES - 1, 0);
    clrtoeol();
    printw(" %s", status_msg[0] ? status_msg :
           "n new  Enter open/link  e edit  b backlinks  / search  q quit");
    attroff(A_REVERSE);
}

static void draw_notes(int width)
{
    int y, visible_total, idx, note_idx;

    visible_total = visible_note_count();
    if (selected_note >= visible_total)
        selected_note = visible_total > 0 ? visible_total - 1 : 0;
    if (selected_note < top_note)
        top_note = selected_note;
    if (selected_note >= top_note + LINES - 3)
        top_note = selected_note - (LINES - 4);
    if (top_note < 0)
        top_note = 0;

    mvprintw(1, 0, "Notes");
    for (y = 2; y < LINES - 1; y++) {
        move(y, 0);
        clrtoeol();
        idx = top_note + y - 2;
        note_idx = visible_note_index(idx);
        if (note_idx < 0)
            continue;
        if (idx == selected_note)
            attron(A_REVERSE);
        mvprintw(y, 0, "%-*.*s", width - 1, width - 1, notes[note_idx].title);
        if (idx == selected_note)
            attroff(A_REVERSE);
    }
}

static void draw_note_text(int x, int width)
{
    int y, i, body_h = LINES - 3;
    char label[16];

    if (current_note < 0) {
        mvprintw(1, x, "No note open");
        return;
    }
    mvprintw(1, x, "%s", notes[current_note].title);
    for (y = 0; y < body_h; y++) {
        i = note_scroll + y;
        if (i >= view_line_count)
            break;
        mvprintw(y + 2, x, "%-*.*s", width, width, view_lines[i]);
    }

    for (i = 0; i < link_count && i < 9; i++) {
        sprintf(label, "[%d]", i + 1);
        mvprintw(LINES - 2, x + i * 8, "%s", label);
    }
}

static int file_contains_link(const char *path, const char *title)
{
    FILE *fp;
    char buf[MAX_LINE + 2];
    char needle[MAX_TITLE + 4];

    copy_string(needle, sizeof(needle), "[[");
    append_string(needle, sizeof(needle), title);
    append_string(needle, sizeof(needle), "]]");
    fp = fopen(path, "r");
    if (!fp)
        return 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (strstr(buf, needle)) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static void draw_backlinks(int x, int width)
{
    int i, y = 2;
    char path[PATH_MAX];

    if (current_note < 0)
        return;
    mvprintw(1, x, "Backlinks to %s", notes[current_note].title);
    for (i = 0; i < note_count && y < LINES - 1; i++) {
        if (i == current_note)
            continue;
        make_path(path, sizeof(path), notes[i].file);
        if (file_contains_link(path, notes[current_note].title)) {
            mvprintw(y++, x, "%-*.*s", width, width, notes[i].title);
        }
    }
}

static void draw_main(void)
{
    int left_w = COLS / 3;
    int x, y;

    if (left_w < 20)
        left_w = 20;
    if (left_w > COLS - 20)
        left_w = COLS / 2;

    erase();
    draw_header();
    draw_notes(left_w);
    for (y = 1; y < LINES - 1; y++)
        mvaddch(y, left_w, ACS_VLINE);
    x = left_w + 2;
    if (show_backlinks)
        draw_backlinks(x, COLS - x - 1);
    else
        draw_note_text(x, COLS - x - 1);
    draw_status();
    refresh();
}

static void draw_editor(void)
{
    int y, line_idx, body_h = LINES - 2;

    if (edit_y < edit_scroll)
        edit_scroll = edit_y;
    if (edit_y >= edit_scroll + body_h)
        edit_scroll = edit_y - body_h + 1;

    erase();
    attron(A_REVERSE);
    mvprintw(0, 0, " editing %s  Ctrl-X save  Esc cancel",
             current_note >= 0 ? notes[current_note].title : "");
    clrtoeol();
    attroff(A_REVERSE);

    for (y = 0; y < body_h; y++) {
        line_idx = edit_scroll + y;
        if (line_idx >= edit_line_count)
            break;
        mvprintw(y + 1, 0, "%4d %.*s", line_idx + 1, COLS - 6,
                 edit_lines[line_idx]);
    }
    move(edit_y - edit_scroll + 1, edit_x + 5);
    refresh();
}

static void editor_insert_char(int ch)
{
    int len = (int)strlen(edit_lines[edit_y]);

    if (len >= MAX_LINE)
        return;
    if (edit_x < 0)
        edit_x = 0;
    if (edit_x > len)
        edit_x = len;
    memmove(edit_lines[edit_y] + edit_x + 1,
            edit_lines[edit_y] + edit_x,
            (size_t)(len - edit_x + 1));
    edit_lines[edit_y][edit_x++] = (char)ch;
}

static void editor_backspace(void)
{
    int len;

    if (edit_x > 0) {
        len = (int)strlen(edit_lines[edit_y]);
        memmove(edit_lines[edit_y] + edit_x - 1,
                edit_lines[edit_y] + edit_x,
                (size_t)(len - edit_x + 1));
        edit_x--;
    } else if (edit_y > 0) {
        int prev_len = (int)strlen(edit_lines[edit_y - 1]);
        int cur_len = (int)strlen(edit_lines[edit_y]);
        if (prev_len + cur_len <= MAX_LINE) {
            strcat(edit_lines[edit_y - 1], edit_lines[edit_y]);
            memmove(edit_lines + edit_y, edit_lines + edit_y + 1,
                    (size_t)(edit_line_count - edit_y - 1) * sizeof(edit_lines[0]));
            edit_line_count--;
            edit_y--;
            edit_x = prev_len;
        }
    }
}

static void editor_newline(void)
{
    char tail[MAX_LINE + 1];

    if (edit_line_count >= MAX_LINES)
        return;
    if (edit_x < 0)
        edit_x = 0;
    if (edit_x > (int)strlen(edit_lines[edit_y]))
        edit_x = (int)strlen(edit_lines[edit_y]);
    copy_string(tail, sizeof(tail), edit_lines[edit_y] + edit_x);
    memmove(edit_lines + edit_y + 2, edit_lines + edit_y + 1,
            (size_t)(edit_line_count - edit_y - 1) * sizeof(edit_lines[0]));
    copy_string(edit_lines[edit_y + 1], sizeof(edit_lines[edit_y + 1]), tail);
    edit_lines[edit_y][edit_x] = '\0';
    edit_line_count++;
    edit_y++;
    edit_x = 0;
}

static void run_editor(void)
{
    int ch, len;

    if (current_note < 0)
        return;
    load_editor();
    curs_set(1);
    for (;;) {
        draw_editor();
        ch = getch();
        len = (int)strlen(edit_lines[edit_y]);
        if (ch == 27) {
            set_status("Edit cancelled");
            break;
        }
        if (ch == CTRL_KEY('x')) {
            save_editor();
            break;
        }
        if (ch == KEY_UP || ch == CTRL_KEY('p')) {
            if (edit_y > 0)
                edit_y--;
            if (edit_x > (int)strlen(edit_lines[edit_y]))
                edit_x = (int)strlen(edit_lines[edit_y]);
        } else if (ch == KEY_DOWN || ch == CTRL_KEY('n')) {
            if (edit_y + 1 < edit_line_count)
                edit_y++;
            if (edit_x > (int)strlen(edit_lines[edit_y]))
                edit_x = (int)strlen(edit_lines[edit_y]);
        } else if (ch == KEY_LEFT || ch == CTRL_KEY('b')) {
            if (edit_x > 0)
                edit_x--;
        } else if (ch == KEY_RIGHT || ch == CTRL_KEY('f')) {
            if (edit_x < len)
                edit_x++;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            editor_backspace();
        } else if (ch == '\n' || ch == '\r') {
            editor_newline();
        } else if (isprint(ch)) {
            editor_insert_char(ch);
        }
    }
    curs_set(0);
}

static void follow_link(int idx)
{
    int i;
    char title[MAX_TITLE];

    if (idx < 0 || idx >= link_count)
        return;
    copy_string(title, sizeof(title), links[idx].label);
    for (i = 0; i < note_count; i++) {
        if (strcmp(notes[i].title, title) == 0) {
            load_note_view(i);
            show_backlinks = 0;
            set_status("Link opened");
            return;
        }
    }
    if (create_note(title)) {
        load_notes();
        for (i = 0; i < note_count; i++) {
            if (strcmp(notes[i].title, title) == 0) {
                load_note_view(i);
                break;
            }
        }
    }
}

static void handle_main_key(int ch)
{
    int visible_total = visible_note_count();
    int idx;
    char input[MAX_TITLE];

    status_msg[0] = '\0';
    if (ch == 'q' || ch == CTRL_KEY('c')) {
        running = 0;
    } else if (ch == KEY_UP || ch == 'k') {
        if (selected_note > 0)
            selected_note--;
    } else if (ch == KEY_DOWN || ch == 'j') {
        if (selected_note + 1 < visible_total)
            selected_note++;
    } else if (ch == KEY_NPAGE || ch == ' ') {
        selected_note += LINES - 4;
        if (selected_note >= visible_total)
            selected_note = visible_total > 0 ? visible_total - 1 : 0;
    } else if (ch == KEY_PPAGE) {
        selected_note -= LINES - 4;
        if (selected_note < 0)
            selected_note = 0;
    } else if (ch == KEY_LEFT || ch == 'h') {
        if (note_scroll > 0)
            note_scroll--;
    } else if (ch == KEY_RIGHT || ch == 'l') {
        note_scroll++;
    } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        idx = visible_note_index(selected_note);
        if (idx == current_note && link_count > 0) {
            follow_link(selected_link);
        } else {
            if (idx >= 0)
                load_note_view(idx);
        }
    } else if (ch >= '1' && ch <= '9') {
        follow_link(ch - '1');
    } else if (ch == '\t') {
        if (link_count > 0)
            selected_link = (selected_link + 1) % link_count;
    } else if (ch == 'n') {
        input[0] = '\0';
        if (prompt_text("New note: ", input, sizeof(input)))
            create_note(input);
    } else if (ch == 'e') {
        run_editor();
    } else if (ch == 'd') {
        delete_current_note();
    } else if (ch == 'r') {
        rename_current_note();
    } else if (ch == '/') {
        if (prompt_text("Search: ", note_filter, sizeof(note_filter))) {
            selected_note = 0;
            top_note = 0;
        }
    } else if (ch == 27) {
        note_filter[0] = '\0';
        show_backlinks = 0;
    } else if (ch == 'b') {
        show_backlinks = !show_backlinks;
    }
}

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char **argv)
{
    int ch;

    if (argc > 1) {
        copy_string(note_dir, sizeof(note_dir), argv[1]);
    } else {
        if (!getcwd(note_dir, sizeof(note_dir))) {
            fprintf(stderr, "memex: could not get current directory\n");
            return 1;
        }
    }

    signal(SIGINT, on_signal);
    load_notes();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    set_status("n new  Enter open/link  e edit  b backlinks  / search  q quit");
    while (running) {
        draw_main();
        ch = getch();
        handle_main_key(ch);
    }

    endwin();
    free_view();
    return 0;
}
