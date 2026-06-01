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
#define MAX_HEADINGS 128
#define MAX_RESULTS 256
#define MAX_BACKLINKS 256
#define MAX_RENDERED 8192
#define STATE_FILE ".memex-state"
#define TRASH_DIR ".trash"

#define CTRL_KEY(x) ((x) & 037)

#ifndef KEY_BTAB
#define KEY_BTAB '\t'
#endif

typedef struct {
    char title[MAX_TITLE];
    char file[MAX_NAME + 4];
} Note;

typedef struct {
    char label[MAX_TITLE];
    char target[MAX_TITLE];
    char heading[MAX_TITLE];
    int line;
} Link;

typedef struct {
    char label[MAX_TITLE];
    char slug[MAX_TITLE];
    int line;
    int level;
} Heading;

typedef struct {
    char text[MAX_LINE + 1];
    int attr;
    int source_line;
    int link_index;
} DisplayLine;

typedef struct {
    int note_index;
    int line;
    char snippet[MAX_LINE + 1];
} SearchResult;

enum {
    PANEL_NOTE = 0,
    PANEL_BACKLINKS = 1,
    PANEL_SEARCH = 2,
    PANEL_OUTLINE = 3
};

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
static Heading headings[MAX_HEADINGS];
static int heading_count = 0;
static int selected_heading = 0;
static DisplayLine rendered_lines[MAX_RENDERED];
static int rendered_line_count = 0;
static int rendered_width = -1;
static SearchResult search_results[MAX_RESULTS];
static int search_result_count = 0;
static int backlink_indices[MAX_BACKLINKS];
static int backlink_count = 0;
static int current_panel = PANEL_NOTE;
static int panel_selected = 0;
static int panel_scroll = 0;
static int show_sidebar = 1;
static int read_mode = 1;
static int running = 1;
static char last_open_title[MAX_TITLE];

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

static void append_nstring(char *dst, size_t dst_size, const char *src, size_t n)
{
    size_t len = strlen(dst);
    size_t to_copy = n;

    if (len >= dst_size || dst_size == 0)
        return;
    if (to_copy > dst_size - len - 1)
        to_copy = dst_size - len - 1;
    if (to_copy == 0)
        return;
    memcpy(dst + len, src, to_copy);
    dst[len + to_copy] = '\0';
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

static int visible_position_for_note(int note_idx)
{
    int i, count = 0;

    for (i = 0; i < note_count; i++) {
        if (!case_contains(notes[i].title, note_filter))
            continue;
        if (i == note_idx)
            return count;
        count++;
    }
    return 0;
}

static int content_width(void)
{
    int left_w;
    int x;

    if (!show_sidebar)
        return COLS - 1;
    left_w = COLS / 3;
    if (left_w < 20)
        left_w = 20;
    if (left_w > COLS - 20)
        left_w = COLS / 2;
    x = left_w + 2;
    return COLS - x - 1;
}

static void make_path(char *out, size_t out_size, const char *file)
{
    copy_string(out, out_size, note_dir);
    append_string(out, out_size, "/");
    append_string(out, out_size, file);
}

static void make_special_path(char *out, size_t out_size, const char *name)
{
    copy_string(out, out_size, note_dir);
    append_string(out, out_size, "/");
    append_string(out, out_size, name);
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

static char *xstrdup(const char *s)
{
    char *p = (char *)malloc(strlen(s) + 1);
    if (p)
        strcpy(p, s);
    return p;
}

static void free_view(void)
{
    int i;

    if (view_lines) {
        for (i = 0; i < view_line_count; i++)
            free(view_lines[i]);
        free(view_lines);
    }
    view_lines = NULL;
    view_line_count = 0;
    link_count = 0;
    heading_count = 0;
    selected_link = 0;
    selected_heading = 0;
    rendered_line_count = 0;
    rendered_width = -1;
}

static int is_blank_line(const char *s)
{
    while (*s) {
        if (!isspace((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

static int is_rule_line(const char *s)
{
    int count = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    while (*s == '-' || *s == '*' || *s == '_') {
        count++;
        s++;
    }
    while (*s == ' ' || *s == '\t')
        s++;
    return *s == '\0' && count >= 3;
}

static void append_char(char *dst, size_t dst_size, size_t *out_len, char ch)
{
    if (*out_len + 1 >= dst_size)
        return;
    dst[*out_len] = ch;
    (*out_len)++;
    dst[*out_len] = '\0';
}

static void trim_copy(const char *src, char *dst, size_t dst_size)
{
    const char *start = src;
    const char *end;
    size_t len;

    while (*start && isspace((unsigned char)*start))
        start++;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
        end--;
    len = (size_t)(end - start);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, start, len);
    dst[len] = '\0';
}

static void normalize_heading(const char *src, char *dst, size_t dst_size)
{
    size_t out_len = 0;
    int dash = 0;
    char ch;

    dst[0] = '\0';
    while (*src && out_len + 1 < dst_size) {
        ch = (char)tolower((unsigned char)*src++);
        if (isalnum((unsigned char)ch)) {
            append_char(dst, dst_size, &out_len, ch);
            dash = 0;
        } else if (!dash && out_len > 0) {
            append_char(dst, dst_size, &out_len, '-');
            dash = 1;
        }
    }
    while (out_len > 0 && dst[out_len - 1] == '-') {
        out_len--;
        dst[out_len] = '\0';
    }
}

static void parse_wiki_link(const char *raw, char *target, size_t target_size,
                            char *heading, size_t heading_size,
                            char *label, size_t label_size)
{
    char temp[MAX_TITLE * 2];
    char *pipe;
    char *hash;

    /* Split [[Note#Heading|Label]] into its target pieces once so render,
     * navigation, and rename-rewrite can all use the same interpretation. */
    copy_string(temp, sizeof(temp), raw);
    pipe = strchr(temp, '|');
    if (pipe) {
        *pipe = '\0';
        trim_copy(pipe + 1, label, label_size);
    } else {
        label[0] = '\0';
    }
    hash = strchr(temp, '#');
    if (hash) {
        *hash = '\0';
        trim_copy(hash + 1, heading, heading_size);
    } else {
        heading[0] = '\0';
    }
    trim_copy(temp, target, target_size);
    if (label[0] == '\0') {
        if (heading[0]) {
            copy_string(label, label_size, target[0] ? target : heading);
            if (target[0] && strlen(label) + strlen(heading) + 2 < label_size) {
                append_string(label, label_size, "#");
                append_string(label, label_size, heading);
            }
        } else {
            copy_string(label, label_size, target);
        }
    }
}

static int first_link_on_line(int line_no)
{
    int i;

    for (i = 0; i < link_count; i++) {
        if (links[i].line == line_no)
            return i;
    }
    return -1;
}

static void scan_links_in_line(const char *line, int line_no)
{
    const char *p = line;
    const char *end;
    int len;
    char raw[MAX_TITLE * 2];

    while ((p = strstr(p, "[[")) != NULL && link_count < MAX_LINKS) {
        p += 2;
        end = strstr(p, "]]");
        if (!end)
            break;
        len = (int)(end - p);
        if (len > 0) {
            if (len >= (int)sizeof(raw))
                len = (int)sizeof(raw) - 1;
            memcpy(raw, p, (size_t)len);
            raw[len] = '\0';
            parse_wiki_link(raw,
                            links[link_count].target, sizeof(links[link_count].target),
                            links[link_count].heading, sizeof(links[link_count].heading),
                            links[link_count].label, sizeof(links[link_count].label));
            links[link_count].line = line_no;
            if (links[link_count].target[0] || links[link_count].heading[0])
                link_count++;
        }
        p = end + 2;
    }
}

static void format_inline_markdown(const char *src, char *dst, size_t dst_size)
{
    size_t out_len = 0;
    const char *end;
    const char *paren_end;
    const char *q;
    char raw[MAX_TITLE * 2];
    char label[MAX_TITLE];
    char target[MAX_TITLE];
    char heading[MAX_TITLE];
    int len;

    dst[0] = '\0';
    while (*src) {
        if (src[0] == '[' && src[1] == '[') {
            end = strstr(src + 2, "]]");
            if (end) {
                len = (int)(end - (src + 2));
                if (len >= (int)sizeof(raw))
                    len = (int)sizeof(raw) - 1;
                memcpy(raw, src + 2, (size_t)len);
                raw[len] = '\0';
                parse_wiki_link(raw, target, sizeof(target),
                                heading, sizeof(heading),
                                label, sizeof(label));
                for (q = label; *q; q++)
                    append_char(dst, dst_size, &out_len, *q);
                src = end + 2;
                continue;
            }
        }
        if ((src[0] == '*' && src[1] == '*')
            || (src[0] == '_' && src[1] == '_')
            || (src[0] == '~' && src[1] == '~')) {
            src += 2;
            continue;
        }
        if (*src == '[') {
            end = strchr(src + 1, ']');
            if (end && end[1] == '(') {
                paren_end = strchr(end + 2, ')');
                if (paren_end) {
                    while (++src < end)
                        append_char(dst, dst_size, &out_len, *src);
                    src = paren_end + 1;
                    continue;
                }
            }
        }
        if (*src == '*' || *src == '_') {
            src++;
            continue;
        }
        if (*src == '`') {
            append_char(dst, dst_size, &out_len, '`');
            src++;
            while (*src && *src != '`') {
                append_char(dst, dst_size, &out_len, *src);
                src++;
            }
            if (*src == '`') {
                append_char(dst, dst_size, &out_len, '`');
                src++;
            }
            continue;
        }
        append_char(dst, dst_size, &out_len, *src);
        src++;
    }
}

static int extract_heading(const char *src, Heading *out)
{
    const char *p = src;
    int level = 0;
    char text[MAX_LINE + 1];

    while (*p == ' ' || *p == '\t')
        p++;
    while (p[level] == '#')
        level++;
    if (level <= 0 || p[level] != ' ')
        return 0;
    format_inline_markdown(p + level + 1, text, sizeof(text));
    trim_copy(text, out->label, sizeof(out->label));
    normalize_heading(out->label, out->slug, sizeof(out->slug));
    out->level = level;
    return out->label[0] != '\0';
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
                selected_note = visible_position_for_note(i);
                break;
            }
        }
    }
}

static void save_state(void)
{
    char path[PATH_MAX];
    FILE *fp;

    make_special_path(path, sizeof(path), STATE_FILE);
    fp = fopen(path, "w");
    if (!fp)
        return;
    fprintf(fp, "sidebar=%d\n", show_sidebar ? 1 : 0);
    fprintf(fp, "read_mode=%d\n", read_mode ? 1 : 0);
    fprintf(fp, "last_note=%s\n",
            current_note >= 0 ? notes[current_note].title : last_open_title);
    fclose(fp);
}

static void load_state(void)
{
    char path[PATH_MAX];
    FILE *fp;
    char buf[MAX_LINE + 2];

    make_special_path(path, sizeof(path), STATE_FILE);
    fp = fopen(path, "r");
    if (!fp)
        return;
    while (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (strncmp(buf, "sidebar=", 8) == 0) {
            show_sidebar = atoi(buf + 8) ? 1 : 0;
        } else if (strncmp(buf, "read_mode=", 10) == 0) {
            read_mode = atoi(buf + 10) ? 1 : 0;
        } else if (strncmp(buf, "last_note=", 10) == 0) {
            copy_string(last_open_title, sizeof(last_open_title), buf + 10);
        }
    }
    fclose(fp);
}

static void add_render_line(const char *text, int attr, int source_line,
                            int link_index)
{
    if (rendered_line_count >= MAX_RENDERED)
        return;
    copy_string(rendered_lines[rendered_line_count].text,
                sizeof(rendered_lines[rendered_line_count].text), text);
    rendered_lines[rendered_line_count].attr = attr;
    rendered_lines[rendered_line_count].source_line = source_line;
    rendered_lines[rendered_line_count].link_index = link_index;
    rendered_line_count++;
}

static void add_wrapped_prefixed_line(const char *prefix, const char *text,
                                      int continuation_indent, int attr,
                                      int source_line, int link_index,
                                      int width)
{
    char line[MAX_LINE + 1];
    char word[MAX_LINE + 1];
    int prefix_len = (int)strlen(prefix);
    int indent = continuation_indent;
    int max_width = width > 4 ? width : 4;
    const char *p = text;
    int first = 1;
    int line_len;
    int word_len;
    int chunk;
    int i;

    if (prefix_len >= max_width)
        prefix_len = max_width - 1;

    for (;;) {
        line[0] = '\0';
        if (first) {
            copy_string(line, sizeof(line), prefix);
            line_len = prefix_len;
        } else {
            for (i = 0; i < indent && i < max_width - 1; i++)
                line[i] = ' ';
            line[i] = '\0';
            line_len = i;
        }

        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '\0') {
            if (line_len > 0 || first)
                add_render_line(line, attr, source_line, link_index);
            return;
        }

        while (*p) {
            word_len = 0;
            while (p[word_len] && !isspace((unsigned char)p[word_len])
                   && word_len < MAX_LINE) {
                word[word_len] = p[word_len];
                word_len++;
            }
            word[word_len] = '\0';

            if (line_len > 0 && line_len + (line_len > (first ? prefix_len : indent) ? 1 : 0)
                + word_len > max_width) {
                break;
            }

            if (word_len > max_width - line_len) {
                if (line_len > (first ? prefix_len : indent))
                    break;
                chunk = max_width - line_len;
                if (chunk <= 0)
                    chunk = 1;
                strncat(line, word, (size_t)chunk);
                add_render_line(line, attr, source_line, link_index);
                p += chunk;
                first = 0;
                break;
            }

            if (line_len > (first ? prefix_len : indent)) {
                strcat(line, " ");
                line_len++;
            }
            strcat(line, word);
            line_len += word_len;
            p += word_len;
            while (*p && isspace((unsigned char)*p))
                p++;
            if (*p == '\0') {
                add_render_line(line, attr, source_line, link_index);
                return;
            }
        }

        add_render_line(line, attr, source_line, link_index);
        first = 0;
    }
}

static void render_note_cache(int width)
{
    int i;
    int in_code_block = 0;
    char text[MAX_LINE + 1];
    char prefix[MAX_LINE + 1];
    const char *p;
    int line_link;
    int level;
    int n;
    Heading temp_heading;
    int num_len;
    int attr;

    if (width <= 0 || current_note < 0)
        return;
    if (rendered_width == width && rendered_line_count > 0)
        return;

    rendered_width = width;
    rendered_line_count = 0;

    /* Pre-render the note into wrapped display lines so scrolling and
     * heading/search jumps can stay line-based in the TUI. */
    for (i = 0; i < view_line_count && rendered_line_count < MAX_RENDERED; i++) {
        p = view_lines[i];
        line_link = first_link_on_line(i);

        while (*p == ' ' || *p == '\t')
            p++;
        if (strcmp(p, "```") == 0) {
            in_code_block = !in_code_block;
            add_render_line("```", A_DIM, i, line_link);
            continue;
        }
        if (in_code_block) {
            add_wrapped_prefixed_line("  ", view_lines[i], 2, A_DIM, i,
                                      line_link, width);
            continue;
        }
        if (is_blank_line(view_lines[i])) {
            add_render_line("", A_NORMAL, i, line_link);
            continue;
        }
        if (is_rule_line(view_lines[i])) {
            add_render_line("----------------------------------------", A_DIM, i,
                            line_link);
            continue;
        }
        if (extract_heading(view_lines[i], &temp_heading)) {
            if (rendered_line_count > 0
                && rendered_lines[rendered_line_count - 1].text[0] != '\0')
                add_render_line("", A_NORMAL, i, line_link);
            add_wrapped_prefixed_line("", temp_heading.label, 0, A_BOLD, i,
                                      line_link, width);
            add_render_line("", A_NORMAL, i, line_link);
            continue;
        }

        p = view_lines[i];
        n = 0;
        while (p[n] == ' ' || p[n] == '\t')
            n++;
        level = n / 2;

        if ((p[n] == '-' || p[n] == '*' || p[n] == '+')
            && p[n + 1] == ' ' && p[n + 2] == '['
            && (p[n + 3] == ' ' || p[n + 3] == 'x' || p[n + 3] == 'X')
            && p[n + 4] == ']' && p[n + 5] == ' ') {
            prefix[0] = '\0';
            while (level-- > 0)
                append_string(prefix, sizeof(prefix), "  ");
            append_string(prefix, sizeof(prefix),
                          p[n + 3] == ' ' ? "[ ] " : "[x] ");
            format_inline_markdown(p + n + 6, text, sizeof(text));
            add_wrapped_prefixed_line(prefix, text, (int)strlen(prefix),
                                      A_NORMAL, i, line_link, width);
            continue;
        }

        if ((p[n] == '-' || p[n] == '*' || p[n] == '+') && p[n + 1] == ' ') {
            prefix[0] = '\0';
            while (level-- > 0)
                append_string(prefix, sizeof(prefix), "  ");
            append_string(prefix, sizeof(prefix), "* ");
            format_inline_markdown(p + n + 2, text, sizeof(text));
            add_wrapped_prefixed_line(prefix, text, (int)strlen(prefix),
                                      A_NORMAL, i, line_link, width);
            continue;
        }

        if (isdigit((unsigned char)p[n])) {
            const char *q = p + n;
            while (isdigit((unsigned char)*q))
                q++;
            if (*q == '.' && q[1] == ' ') {
                prefix[0] = '\0';
                while (level-- > 0)
                    append_string(prefix, sizeof(prefix), "  ");
                num_len = (int)(q - (p + n));
                strncat(prefix, p + n, (size_t)num_len);
                append_string(prefix, sizeof(prefix), ". ");
                format_inline_markdown(q + 2, text, sizeof(text));
                add_wrapped_prefixed_line(prefix, text, (int)strlen(prefix),
                                          A_NORMAL, i, line_link, width);
                continue;
            }
        }

        if (p[n] == '>') {
            prefix[0] = '\0';
            while (level-- > 0)
                append_string(prefix, sizeof(prefix), "  ");
            append_string(prefix, sizeof(prefix), "| ");
            format_inline_markdown(p + n + (p[n + 1] == ' ' ? 2 : 1),
                                   text, sizeof(text));
            add_wrapped_prefixed_line(prefix, text, (int)strlen(prefix),
                                      A_DIM, i, line_link, width);
            continue;
        }

        attr = strchr(view_lines[i], '`') ? A_UNDERLINE : A_NORMAL;
        format_inline_markdown(view_lines[i], text, sizeof(text));
        add_wrapped_prefixed_line("", text, 0, attr, i, line_link, width);
    }
}

static int display_index_for_source_line(int source_line)
{
    int i;

    for (i = 0; i < rendered_line_count; i++) {
        if (rendered_lines[i].source_line >= source_line)
            return i;
    }
    return 0;
}

static int heading_line_for_slug(const char *slug)
{
    int i;

    for (i = 0; i < heading_count; i++) {
        if (strcmp(headings[i].slug, slug) == 0)
            return headings[i].line;
    }
    return -1;
}

static void jump_to_source_line(int source_line)
{
    if (source_line < 0)
        source_line = 0;
    if (read_mode) {
        render_note_cache(content_width());
        note_scroll = display_index_for_source_line(source_line);
    } else {
        note_scroll = source_line;
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
    current_panel = PANEL_NOTE;
    panel_selected = 0;
    panel_scroll = 0;

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
        if (heading_count < MAX_HEADINGS && extract_heading(buf, &headings[heading_count])) {
            headings[heading_count].line = view_line_count;
            heading_count++;
        }
        view_line_count++;
    }
    fclose(fp);

    copy_string(last_open_title, sizeof(last_open_title), notes[idx].title);
    selected_note = visible_position_for_note(idx);
    save_state();
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

static int ensure_trash_dir(void)
{
    char path[PATH_MAX];
    struct stat st;

    make_special_path(path, sizeof(path), TRASH_DIR);
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode);
    if (mkdir(path, 0777) == 0)
        return 1;
    return 0;
}

static void build_trash_path(const char *file, char *out, size_t out_size)
{
    char dir_path[PATH_MAX];
    char candidate[PATH_MAX];
    int attempt = 0;
    FILE *fp;

    make_special_path(dir_path, sizeof(dir_path), TRASH_DIR);
    for (;;) {
        if (attempt == 0) {
            copy_string(candidate, sizeof(candidate), dir_path);
            append_string(candidate, sizeof(candidate), "/");
            append_string(candidate, sizeof(candidate), file);
        } else {
            sprintf(candidate, "%s/%d-%s", dir_path, attempt, file);
        }
        fp = fopen(candidate, "r");
        if (!fp)
            break;
        fclose(fp);
        attempt++;
    }
    copy_string(out, out_size, candidate);
}

static void delete_current_note(void)
{
    char path[PATH_MAX];
    char trash_path[PATH_MAX];

    if (current_note < 0)
        return;
    if (!ensure_trash_dir()) {
        set_status("Could not create trash folder");
        return;
    }
    make_path(path, sizeof(path), notes[current_note].file);
    build_trash_path(notes[current_note].file, trash_path, sizeof(trash_path));
    if (rename(path, trash_path) != 0) {
        set_status("Could not move note to trash");
        return;
    }
    free_view();
    current_note = -1;
    load_notes();
    save_state();
    set_status("Note moved to trash");
}

static int rewrite_file_links(const char *file_name, const char *old_title,
                              const char *new_title)
{
    char path[PATH_MAX];
    char temp_path[PATH_MAX];
    FILE *in;
    FILE *out;
    char buf[MAX_LINE + 2];
    char rewritten[MAX_LINE * 2];
    const char *p;
    const char *end;
    char raw[MAX_TITLE * 2];
    char target[MAX_TITLE];
    char heading[MAX_TITLE];
    char label[MAX_TITLE];
    int len;
    int changed = 0;

    make_path(path, sizeof(path), file_name);
    sprintf(temp_path, "%s/.memex-rewrite.tmp", note_dir);

    in = fopen(path, "r");
    if (!in)
        return 0;
    out = fopen(temp_path, "w");
    if (!out) {
        fclose(in);
        return 0;
    }

    /* Rewrite wiki-link targets conservatively: preserve aliases/headings and
     * only touch links whose resolved note target matches the renamed note. */
    while (fgets(buf, sizeof(buf), in)) {
        rewritten[0] = '\0';
        p = buf;
        while ((end = strstr(p, "[[")) != NULL) {
            append_nstring(rewritten, sizeof(rewritten), p, (size_t)(end - p));
            p = end + 2;
            end = strstr(p, "]]");
            if (!end) {
                append_string(rewritten, sizeof(rewritten), "[[");
                append_string(rewritten, sizeof(rewritten), p);
                p += strlen(p);
                break;
            }
            len = (int)(end - p);
            if (len >= (int)sizeof(raw))
                len = (int)sizeof(raw) - 1;
            memcpy(raw, p, (size_t)len);
            raw[len] = '\0';
            parse_wiki_link(raw, target, sizeof(target),
                            heading, sizeof(heading),
                            label, sizeof(label));
            append_string(rewritten, sizeof(rewritten), "[[");
            if (strcmp(target, old_title) == 0) {
                append_string(rewritten, sizeof(rewritten), new_title);
                changed = 1;
            } else {
                append_string(rewritten, sizeof(rewritten), target);
            }
            if (heading[0]) {
                append_string(rewritten, sizeof(rewritten), "#");
                append_string(rewritten, sizeof(rewritten), heading);
            }
            if (label[0]) {
                if (!(strcmp(label, target) == 0 && heading[0] == '\0')) {
                    char default_label[MAX_TITLE];

                    default_label[0] = '\0';
                    copy_string(default_label, sizeof(default_label), target);
                    if (heading[0]) {
                        append_string(default_label, sizeof(default_label), "#");
                        append_string(default_label, sizeof(default_label), heading);
                    }
                    if (strcmp(label, default_label) != 0) {
                        append_string(rewritten, sizeof(rewritten), "|");
                        append_string(rewritten, sizeof(rewritten), label);
                    }
                }
            }
            append_string(rewritten, sizeof(rewritten), "]]");
            p = end + 2;
        }
        append_string(rewritten, sizeof(rewritten), p);
        fputs(rewritten, out);
    }
    fclose(in);
    fclose(out);

    if (changed) {
        if (rename(temp_path, path) != 0)
            unlink(temp_path);
    } else {
        unlink(temp_path);
    }
    return changed;
}

static void rewrite_links_for_rename(const char *old_title, const char *new_title)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(note_dir);
    if (!dir)
        return;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_md_suffix(ent->d_name))
            continue;
        rewrite_file_links(ent->d_name, old_title, new_title);
    }
    closedir(dir);
}

static void rename_current_note(void)
{
    char title[MAX_TITLE];
    char clean[MAX_TITLE];
    char file[MAX_NAME + 4];
    char old_path[PATH_MAX], new_path[PATH_MAX];
    char old_title[MAX_TITLE];
    FILE *fp;

    if (current_note < 0)
        return;
    copy_string(title, sizeof(title), notes[current_note].title);
    if (!prompt_text("Rename: ", title, sizeof(title)))
        return;
    sanitize_title(title, clean, sizeof(clean));
    title_to_file(clean, file, sizeof(file));
    make_path(old_path, sizeof(old_path), notes[current_note].file);
    make_path(new_path, sizeof(new_path), file);
    if (strcmp(old_path, new_path) == 0)
        return;
    fp = fopen(new_path, "r");
    if (fp) {
        fclose(fp);
        set_status("Target note already exists");
        return;
    }
    copy_string(old_title, sizeof(old_title), notes[current_note].title);
    if (rename(old_path, new_path) != 0) {
        set_status("Could not rename note");
        return;
    }
    rewrite_links_for_rename(old_title, clean);
    load_notes();
    set_status("Note renamed and links updated");
    {
        int i;

        for (i = 0; i < note_count; i++) {
            if (strcmp(notes[i].title, clean) == 0) {
                load_note_view(i);
                break;
            }
        }
    }
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
    printw(" memex  mode:%s  view:%s  dir:%s  /:%s",
           read_mode ? "read" : "write",
           current_panel == PANEL_NOTE ? "note" :
           current_panel == PANEL_BACKLINKS ? "backlinks" :
           current_panel == PANEL_SEARCH ? "search" : "outline",
           note_dir, note_filter[0] ? note_filter : "-");
    attroff(A_REVERSE);
}

static void draw_status(void)
{
    const char *msg = status_msg[0] ? status_msg :
        "m mode  n new  Enter open/follow  e edit  f find  o outline  b backlinks  s sidebar  / filter  q quit";

    attron(A_REVERSE);
    move(LINES - 1, 0);
    clrtoeol();
    printw(" %s", msg);
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
    int total_lines;
    int attr;
    int is_selected_link;

    if (current_note < 0) {
        mvprintw(1, x, "No note open");
        return;
    }

    mvprintw(1, x, "%s [%s]", notes[current_note].title,
             read_mode ? "read" : "write");

    if (read_mode) {
        render_note_cache(width - 1);
        total_lines = rendered_line_count;
        if (note_scroll > total_lines - 1)
            note_scroll = total_lines > 0 ? total_lines - 1 : 0;
        for (y = 0; y < body_h; y++) {
            i = note_scroll + y;
            if (i >= rendered_line_count)
                break;
            attr = rendered_lines[i].attr;
            is_selected_link = selected_link >= 0
                && selected_link < link_count
                && rendered_lines[i].link_index == selected_link;
            move(y + 2, x);
            clrtoeol();
            if (attr != A_NORMAL)
                attron(attr);
            if (is_selected_link)
                attron(A_REVERSE);
            mvprintw(y + 2, x, "%-*.*s", width, width,
                     rendered_lines[i].text);
            if (is_selected_link)
                attroff(A_REVERSE);
            if (attr != A_NORMAL)
                attroff(attr);
        }
    } else {
        total_lines = view_line_count;
        if (note_scroll > total_lines - 1)
            note_scroll = total_lines > 0 ? total_lines - 1 : 0;
        for (y = 0; y < body_h; y++) {
            i = note_scroll + y;
            if (i >= view_line_count)
                break;
            move(y + 2, x);
            clrtoeol();
            is_selected_link = selected_link >= 0
                && selected_link < link_count
                && links[selected_link].line == i;
            if (is_selected_link)
                attron(A_REVERSE);
            mvprintw(y + 2, x, "%-*.*s", width, width, view_lines[i]);
            if (is_selected_link)
                attroff(A_REVERSE);
        }
    }

    move(LINES - 2, x);
    clrtoeol();
    if (link_count > 0 && selected_link >= 0 && selected_link < link_count) {
        printw("Link: %s", links[selected_link].label);
        if (links[selected_link].target[0]) {
            printw(" -> %s", links[selected_link].target);
            if (links[selected_link].heading[0])
                printw("#%s", links[selected_link].heading);
        }
    } else {
        printw("Tab/Shift-Tab selects links");
    }
}

static int file_contains_link(const char *path, const char *title)
{
    FILE *fp;
    char buf[MAX_LINE + 2];
    char raw[MAX_TITLE * 2];
    char target[MAX_TITLE];
    char heading[MAX_TITLE];
    char label[MAX_TITLE];
    const char *p;
    const char *end;
    int len;

    fp = fopen(path, "r");
    if (!fp)
        return 0;
    while (fgets(buf, sizeof(buf), fp)) {
        p = buf;
        while ((p = strstr(p, "[[")) != NULL) {
            p += 2;
            end = strstr(p, "]]");
            if (!end)
                break;
            len = (int)(end - p);
            if (len >= (int)sizeof(raw))
                len = (int)sizeof(raw) - 1;
            memcpy(raw, p, (size_t)len);
            raw[len] = '\0';
            parse_wiki_link(raw, target, sizeof(target),
                            heading, sizeof(heading),
                            label, sizeof(label));
            if (strcmp(target, title) == 0) {
                fclose(fp);
                return 1;
            }
            p = end + 2;
        }
    }
    fclose(fp);
    return 0;
}

static void build_backlinks(void)
{
    int i;
    char path[PATH_MAX];

    backlink_count = 0;
    if (current_note < 0)
        return;
    for (i = 0; i < note_count && backlink_count < MAX_BACKLINKS; i++) {
        if (i == current_note)
            continue;
        make_path(path, sizeof(path), notes[i].file);
        if (file_contains_link(path, notes[current_note].title))
            backlink_indices[backlink_count++] = i;
    }
    panel_selected = 0;
    panel_scroll = 0;
    current_panel = PANEL_BACKLINKS;
}

static void run_full_text_search(const char *query)
{
    int i;
    FILE *fp;
    char path[PATH_MAX];
    char buf[MAX_LINE + 2];
    int line_no;
    char plain[MAX_LINE + 1];

    search_result_count = 0;
    /* Search remains scan-based for now; v1 indexed search can replace this
     * without changing the result view or jump behavior. */
    for (i = 0; i < note_count && search_result_count < MAX_RESULTS; i++) {
        make_path(path, sizeof(path), notes[i].file);
        fp = fopen(path, "r");
        if (!fp)
            continue;
        line_no = 0;
        while (fgets(buf, sizeof(buf), fp) && search_result_count < MAX_RESULTS) {
            buf[strcspn(buf, "\r\n")] = '\0';
            format_inline_markdown(buf, plain, sizeof(plain));
            if (case_contains(plain, query) || case_contains(buf, query)) {
                search_results[search_result_count].note_index = i;
                search_results[search_result_count].line = line_no;
                copy_string(search_results[search_result_count].snippet,
                            sizeof(search_results[search_result_count].snippet),
                            plain[0] ? plain : buf);
                search_result_count++;
            }
            line_no++;
        }
        fclose(fp);
    }
    panel_selected = 0;
    panel_scroll = 0;
    current_panel = PANEL_SEARCH;
    if (search_result_count == 0)
        set_status("No content matches");
    else
        set_status("Content search results");
}

static void open_outline(void)
{
    if (current_note < 0 || heading_count == 0) {
        set_status("No headings in note");
        return;
    }
    panel_selected = selected_heading;
    panel_scroll = 0;
    current_panel = PANEL_OUTLINE;
}

static void draw_backlinks(int x, int width)
{
    int i, y, idx;

    mvprintw(1, x, "Backlinks to %s", current_note >= 0 ? notes[current_note].title : "");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= backlink_count)
            continue;
        i = backlink_indices[idx];
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, notes[i].title);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_search_results(int x, int width)
{
    int idx;
    int y;
    char line[MAX_LINE + 1];

    mvprintw(1, x, "Search results");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= search_result_count)
            continue;
        sprintf(line, "%s:%d  %s",
                notes[search_results[idx].note_index].title,
                search_results[idx].line + 1,
                search_results[idx].snippet);
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, line);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_outline(int x, int width)
{
    int idx;
    int y;
    char line[MAX_LINE + 1];
    int indent;
    int i;

    mvprintw(1, x, "Outline");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= heading_count)
            continue;
        line[0] = '\0';
        indent = headings[idx].level - 1;
        for (i = 0; i < indent; i++)
            append_string(line, sizeof(line), "  ");
        append_string(line, sizeof(line), headings[idx].label);
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, line);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_main(void)
{
    int left_w = COLS / 3;
    int x, y;

    erase();
    draw_header();
    if (show_sidebar) {
        if (left_w < 20)
            left_w = 20;
        if (left_w > COLS - 20)
            left_w = COLS / 2;

        draw_notes(left_w);
        for (y = 1; y < LINES - 1; y++)
            mvaddch(y, left_w, ACS_VLINE);
        x = left_w + 2;
    } else {
        x = 0;
    }
    if (current_panel == PANEL_BACKLINKS)
        draw_backlinks(x, COLS - x - 1);
    else if (current_panel == PANEL_SEARCH)
        draw_search_results(x, COLS - x - 1);
    else if (current_panel == PANEL_OUTLINE)
        draw_outline(x, COLS - x - 1);
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

static void open_note_at_line(int idx, int source_line)
{
    if (idx < 0 || idx >= note_count)
        return;
    load_note_view(idx);
    jump_to_source_line(source_line);
}

static void follow_link(int idx)
{
    int i;
    char heading_slug[MAX_TITLE];
    int line = 0;

    if (idx < 0 || idx >= link_count)
        return;
    if (links[idx].target[0] == '\0') {
        set_status("Link target missing");
        return;
    }
    for (i = 0; i < note_count; i++) {
        if (strcmp(notes[i].title, links[idx].target) == 0) {
            load_note_view(i);
            if (links[idx].heading[0]) {
                normalize_heading(links[idx].heading,
                                  heading_slug, sizeof(heading_slug));
                line = heading_line_for_slug(heading_slug);
                if (line >= 0)
                    jump_to_source_line(line);
            }
            set_status("Link opened");
            return;
        }
    }
    if (create_note(links[idx].target)) {
        load_notes();
        for (i = 0; i < note_count; i++) {
            if (strcmp(notes[i].title, links[idx].target) == 0) {
                load_note_view(i);
                break;
            }
        }
    }
}

static void panel_move(int delta, int total)
{
    int window = LINES - 3;

    if (total <= 0) {
        panel_selected = 0;
        panel_scroll = 0;
        return;
    }
    panel_selected += delta;
    if (panel_selected < 0)
        panel_selected = 0;
    if (panel_selected >= total)
        panel_selected = total - 1;
    if (panel_selected < panel_scroll)
        panel_scroll = panel_selected;
    if (panel_selected >= panel_scroll + window)
        panel_scroll = panel_selected - window + 1;
    if (panel_scroll < 0)
        panel_scroll = 0;
}

static void handle_panel_key(int ch)
{
    int idx;

    if (ch == 27) {
        current_panel = PANEL_NOTE;
        return;
    }
    if (ch == KEY_UP || ch == 'k') {
        if (current_panel == PANEL_BACKLINKS)
            panel_move(-1, backlink_count);
        else if (current_panel == PANEL_SEARCH)
            panel_move(-1, search_result_count);
        else if (current_panel == PANEL_OUTLINE)
            panel_move(-1, heading_count);
        return;
    }
    if (ch == KEY_DOWN || ch == 'j') {
        if (current_panel == PANEL_BACKLINKS)
            panel_move(1, backlink_count);
        else if (current_panel == PANEL_SEARCH)
            panel_move(1, search_result_count);
        else if (current_panel == PANEL_OUTLINE)
            panel_move(1, heading_count);
        return;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (current_panel == PANEL_BACKLINKS && panel_selected < backlink_count) {
            idx = backlink_indices[panel_selected];
            open_note_at_line(idx, 0);
        } else if (current_panel == PANEL_SEARCH && panel_selected < search_result_count) {
            open_note_at_line(search_results[panel_selected].note_index,
                              search_results[panel_selected].line);
        } else if (current_panel == PANEL_OUTLINE && panel_selected < heading_count) {
            selected_heading = panel_selected;
            current_panel = PANEL_NOTE;
            jump_to_source_line(headings[panel_selected].line);
        }
    }
}

static void handle_main_key(int ch)
{
    int visible_total = visible_note_count();
    int idx;
    char input[MAX_TITLE];

    if (current_panel != PANEL_NOTE) {
        handle_panel_key(ch);
        return;
    }

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
        } else if (idx >= 0) {
            load_note_view(idx);
        }
    } else if (ch == '\t') {
        if (link_count > 0)
            selected_link = (selected_link + 1) % link_count;
    } else if (ch == KEY_BTAB) {
        if (link_count > 0)
            selected_link = (selected_link + link_count - 1) % link_count;
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
        if (prompt_text("Filter titles: ", note_filter, sizeof(note_filter))) {
            selected_note = 0;
            top_note = 0;
        }
    } else if (ch == 'f') {
        input[0] = '\0';
        if (prompt_text("Find in notes: ", input, sizeof(input)))
            run_full_text_search(input);
    } else if (ch == 27) {
        note_filter[0] = '\0';
    } else if (ch == 'b') {
        build_backlinks();
    } else if (ch == 'o') {
        open_outline();
    } else if (ch == 'm') {
        read_mode = !read_mode;
        save_state();
    } else if (ch == 's') {
        show_sidebar = !show_sidebar;
        save_state();
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
    int i;

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
    load_state();

    if (last_open_title[0]) {
        for (i = 0; i < note_count; i++) {
            if (strcmp(notes[i].title, last_open_title) == 0) {
                load_note_view(i);
                break;
            }
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    set_status("m mode  n new  Enter open/follow  e edit  f find  o outline  b backlinks  s sidebar  / filter  q quit");
    while (running) {
        draw_main();
        ch = getch();
        handle_main_key(ch);
    }

    save_state();
    endwin();
    free_view();
    return 0;
}
