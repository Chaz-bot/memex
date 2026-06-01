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
#include <time.h>
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
#define MAX_TAGS_PER_NOTE 16
#define MAX_TAGS 256
#define MAX_ALIASES 16
#define MAX_DIRS 256
#define MAX_SIDEBAR_ITEMS 1024
#define MAX_PATH_PART 96
#define MAX_HISTORY 128
#define MAX_RECENT 12
#define MAX_UNDO 32
#define MAX_SAVED_SEARCHES 32
#define MAX_MENTIONS 256
#define MAX_INFO_LINES 32
#define MAX_COMMANDS 20
#define STATE_FILE ".memex-state"
#define CONFIG_FILE ".memexrc"
#define SAVED_SEARCH_FILE ".memex-searches"
#define TRASH_DIR ".trash"
#define TEMPLATE_DIR ".templates"
#define DEFAULT_TEMPLATE "default.md"
#define DAILY_TEMPLATE "daily.md"
#define DAILY_FORMAT_FILE ".memex-daily-format"

#define CTRL_KEY(x) ((x) & 037)

#ifndef KEY_BTAB
#define KEY_BTAB '\t'
#endif

typedef struct {
    char title[MAX_TITLE];
    char file[PATH_MAX];
    char rel_path[PATH_MAX];
    char dir_path[PATH_MAX];
    char display_title[MAX_TITLE];
    char tags[MAX_TAGS_PER_NOTE][MAX_TITLE];
    int tag_count;
    char aliases[MAX_ALIASES][MAX_TITLE];
    int alias_count;
    long mtime;
    long ctime;
    int pinned;
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

typedef struct {
    char *raw_text;
    char *plain_text;
    int outbound[MAX_LINKS];
    int outbound_count;
    int backlinks[MAX_BACKLINKS];
    int backlink_count;
    int mentions[MAX_MENTIONS];
    int mention_count;
} NoteIndex;

typedef struct {
    char name[MAX_TITLE];
    int count;
} TagInfo;

typedef struct {
    char path[PATH_MAX];
    char name[MAX_PATH_PART];
    int depth;
    int expanded;
} DirectoryInfo;

typedef struct {
    int kind;
    int note_index;
    int dir_index;
    char label[MAX_LINE + 1];
    int depth;
} SidebarItem;

typedef struct {
    char title[MAX_TITLE];
    int line;
} HistoryEntry;

typedef struct {
    char name[MAX_TITLE];
    char query[MAX_FILTER];
} SavedSearch;

typedef struct {
    const char *label;
    int action;
} CommandEntry;

enum {
    PANEL_NOTE = 0,
    PANEL_BACKLINKS = 1,
    PANEL_SEARCH = 2,
    PANEL_OUTLINE = 3,
    PANEL_TAGS = 4,
    PANEL_MENTIONS = 5,
    PANEL_INFO = 6,
    PANEL_SAVED = 7,
    PANEL_COMMANDS = 8
};

enum {
    SORT_ALPHA = 0,
    SORT_MTIME = 1,
    SORT_CTIME = 2
};

enum {
    SIDEBAR_KIND_SECTION = 0,
    SIDEBAR_KIND_DIR = 1,
    SIDEBAR_KIND_NOTE = 2
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
static NoteIndex note_index[MAX_NOTES];
static int backlink_indices[MAX_BACKLINKS];
static int backlink_count = 0;
static int mention_indices[MAX_MENTIONS];
static int mention_count = 0;
static TagInfo tags[MAX_TAGS];
static int tag_count = 0;
static int selected_tag = 0;
static char active_tag[MAX_TITLE];
static DirectoryInfo dirs[MAX_DIRS];
static int dir_count = 0;
static SidebarItem sidebar_items[MAX_SIDEBAR_ITEMS];
static int sidebar_item_count = 0;
static int sort_mode = SORT_ALPHA;
static HistoryEntry history_back[MAX_HISTORY];
static int history_back_count = 0;
static HistoryEntry history_forward[MAX_HISTORY];
static int history_forward_count = 0;
static int recent_notes[MAX_RECENT];
static int recent_note_count = 0;
static int current_panel = PANEL_NOTE;
static int panel_selected = 0;
static int panel_scroll = 0;
static SavedSearch saved_searches[MAX_SAVED_SEARCHES];
static int saved_search_count = 0;
static int selected_saved_search = 0;
static char current_search_query[MAX_FILTER];
static int info_line_count = 0;
static char info_lines[MAX_INFO_LINES][MAX_LINE + 1];
static int selected_command = 0;
static int show_sidebar = 1;
static int read_mode = 1;
static int running = 1;
static char last_open_title[MAX_TITLE];
static char startup_mode[MAX_TITLE] = "last";
static char theme_name[MAX_TITLE] = "plain";
static char trash_dir_name[MAX_TITLE] = TRASH_DIR;
static char template_dir_name[MAX_TITLE] = TEMPLATE_DIR;
static int startup_applied = 0;

static int key_new_note = 'n';
static int key_template_note = 't';
static int key_daily_note = 'D';
static int key_edit_note = 'e';
static int key_delete_note = 'd';
static int key_rename_note = 'r';
static int key_filter_titles = '/';
static int key_full_text_search = 'f';
static int key_backlinks = 'b';
static int key_mentions = 'u';
static int key_outline = 'o';
static int key_tags = 'g';
static int key_saved_searches = 'v';
static int key_command_palette = 'p';
static int key_note_info = 'i';
static int key_toggle_mode = 'm';
static int key_toggle_sidebar = 's';
static int key_cycle_sort = 'S';
static int key_cycle_theme = 'T';
static int key_history_back = '[';
static int key_history_forward = ']';
static int key_save_search = 'A';

static char edit_lines[MAX_LINES][MAX_LINE + 1];
static int edit_line_count = 0;
static int edit_y = 0;
static int edit_x = 0;
static int edit_scroll = 0;
static int edit_dirty = 0;
static int edit_quit_confirm = 0;
static int edit_find_line = -1;
static char edit_find_query[MAX_FILTER];
static char *undo_stack[MAX_UNDO];
static int undo_count = 0;
static char *redo_stack[MAX_UNDO];
static int redo_count = 0;
static char autocomplete_prefix[MAX_TITLE];
static int autocomplete_last_match = -1;

static void trim_copy(const char *src, char *dst, size_t dst_size);
static int find_note_by_target(const char *target);
static void build_note_indices(void);
static void init_theme(void);
static int case_equals(const char *a, const char *b);

static const char *theme_names[] = {
    "plain",
    "amber",
    "forest",
    "ocean"
};

#define THEME_COUNT ((int)(sizeof(theme_names) / sizeof(theme_names[0])))

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

static int theme_index_from_name(const char *name)
{
    int i;

    for (i = 0; i < THEME_COUNT; i++) {
        if (case_equals(name, theme_names[i]))
            return i;
    }
    return 0;
}

static int is_word_boundary_char(int ch)
{
    return ch == '\0' || isspace((unsigned char)ch) || ispunct((unsigned char)ch);
}

static int case_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
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

static int case_starts_with(const char *s, const char *prefix)
{
    int i;

    for (i = 0; prefix[i]; i++) {
        if (s[i] == '\0')
            return 0;
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}

static int text_contains_phrase(const char *s, const char *needle)
{
    int i, j;
    char a, b;

    if (needle[0] == '\0')
        return 0;
    for (i = 0; s[i]; i++) {
        if (i > 0 && !is_word_boundary_char((unsigned char)s[i - 1]))
            continue;
        for (j = 0; needle[j]; j++) {
            a = (char)tolower((unsigned char)s[i + j]);
            b = (char)tolower((unsigned char)needle[j]);
            if (a != b || s[i + j] == '\0')
                break;
        }
        if (needle[j] == '\0' && is_word_boundary_char((unsigned char)s[i + j]))
            return 1;
    }
    return 0;
}

static int note_matches_active_tag(int note_idx)
{
    int i;

    if (active_tag[0] == '\0')
        return 1;
    if (note_idx < 0 || note_idx >= note_count)
        return 0;
    for (i = 0; i < notes[note_idx].tag_count; i++) {
        if (strcmp(notes[note_idx].tags[i], active_tag) == 0)
            return 1;
    }
    return 0;
}

static int note_matches_filter(int note_idx)
{
    if (note_idx < 0 || note_idx >= note_count)
        return 0;
    if (!note_matches_active_tag(note_idx))
        return 0;
    return case_contains(notes[note_idx].title, note_filter)
        || case_contains(notes[note_idx].display_title, note_filter)
        || case_contains(notes[note_idx].rel_path, note_filter);
}

static int path_depth(const char *path)
{
    int depth = 0;

    while (*path) {
        if (*path == '/')
            depth++;
        path++;
    }
    return depth;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

static int find_dir_index(const char *path)
{
    int i;

    for (i = 0; i < dir_count; i++) {
        if (strcmp(dirs[i].path, path) == 0)
            return i;
    }
    return -1;
}

static int ensure_dir_entry(const char *path)
{
    int idx;

    if (path[0] == '\0')
        return -1;
    idx = find_dir_index(path);
    if (idx >= 0)
        return idx;
    if (dir_count >= MAX_DIRS)
        return -1;
    copy_string(dirs[dir_count].path, sizeof(dirs[dir_count].path), path);
    copy_string(dirs[dir_count].name, sizeof(dirs[dir_count].name),
                path_basename(path));
    dirs[dir_count].depth = path_depth(path);
    dirs[dir_count].expanded = 1;
    dir_count++;
    return dir_count - 1;
}

static int parent_dir_visible(const char *path)
{
    char parent[PATH_MAX];
    char *slash;
    int idx;

    if (path[0] == '\0')
        return 1;
    copy_string(parent, sizeof(parent), path);
    slash = strrchr(parent, '/');
    if (slash)
        *slash = '\0';
    else
        parent[0] = '\0';
    if (parent[0] == '\0')
        return 1;
    idx = find_dir_index(parent);
    if (idx < 0)
        return 1;
    return dirs[idx].expanded && parent_dir_visible(parent);
}

static int note_visible_in_sidebar(int note_idx)
{
    if (!note_matches_filter(note_idx))
        return 0;
    return parent_dir_visible(notes[note_idx].dir_path);
}

static int sidebar_note_index(int visible_pos)
{
    int i, count = 0;

    for (i = 0; i < sidebar_item_count; i++) {
        if (sidebar_items[i].kind != SIDEBAR_KIND_NOTE)
            continue;
        if (count == visible_pos)
            return sidebar_items[i].note_index;
        count++;
    }
    return -1;
}

static int visible_note_index(int visible_pos)
{
    return sidebar_note_index(visible_pos);
}

static int sidebar_selected_item_index(void)
{
    if (selected_note < 0)
        selected_note = 0;
    if (selected_note >= sidebar_item_count)
        selected_note = sidebar_item_count > 0 ? sidebar_item_count - 1 : 0;
    return selected_note;
}

static void clear_tag_cache(void)
{
    tag_count = 0;
}

static void add_global_tag(const char *name)
{
    int i;

    if (name[0] == '\0')
        return;
    for (i = 0; i < tag_count; i++) {
        if (strcmp(tags[i].name, name) == 0) {
            tags[i].count++;
            return;
        }
    }
    if (tag_count >= MAX_TAGS)
        return;
    copy_string(tags[tag_count].name, sizeof(tags[tag_count].name), name);
    tags[tag_count].count = 1;
    tag_count++;
}

static int visible_note_count(void)
{
    int i, count = 0;

    for (i = 0; i < sidebar_item_count; i++) {
        if (sidebar_items[i].kind == SIDEBAR_KIND_NOTE)
            count++;
    }
    return count;
}

static int visible_position_for_note(int note_idx)
{
    int i;

    for (i = 0; i < sidebar_item_count; i++) {
        if (sidebar_items[i].kind == SIDEBAR_KIND_NOTE
            && sidebar_items[i].note_index == note_idx)
            return i;
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

static void split_rel_path(const char *rel_path, char *dir_path, size_t dir_size,
                           char *file_name, size_t file_size)
{
    const char *slash = strrchr(rel_path, '/');

    if (slash) {
        size_t n = (size_t)(slash - rel_path);

        if (n >= dir_size)
            n = dir_size - 1;
        memcpy(dir_path, rel_path, n);
        dir_path[n] = '\0';
        copy_string(file_name, file_size, slash + 1);
    } else {
        dir_path[0] = '\0';
        copy_string(file_name, file_size, rel_path);
    }
}

static void strip_md_suffix(const char *name, char *out, size_t out_size)
{
    copy_string(out, out_size, name);
    if (has_md_suffix(out))
        out[strlen(out) - 3] = '\0';
}

static void normalize_tag(const char *src, char *dst, size_t dst_size)
{
    size_t out_len = 0;
    char ch;

    while (*src && out_len + 1 < dst_size) {
        ch = (char)tolower((unsigned char)*src++);
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '/')
            dst[out_len++] = ch;
    }
    dst[out_len] = '\0';
}

static void note_add_tag(Note *note, const char *raw)
{
    char tag[MAX_TITLE];
    int i;

    normalize_tag(raw, tag, sizeof(tag));
    if (tag[0] == '\0')
        return;
    for (i = 0; i < note->tag_count; i++) {
        if (strcmp(note->tags[i], tag) == 0)
            return;
    }
    if (note->tag_count >= MAX_TAGS_PER_NOTE)
        return;
    copy_string(note->tags[note->tag_count], sizeof(note->tags[note->tag_count]), tag);
    note->tag_count++;
}

static void note_add_alias(Note *note, const char *raw)
{
    char alias[MAX_TITLE];
    int i;

    trim_copy(raw, alias, sizeof(alias));
    if (alias[0] == '\0')
        return;
    for (i = 0; i < note->alias_count; i++) {
        if (strcmp(note->aliases[i], alias) == 0)
            return;
    }
    if (note->alias_count >= MAX_ALIASES)
        return;
    copy_string(note->aliases[note->alias_count],
                sizeof(note->aliases[note->alias_count]), alias);
    note->alias_count++;
}

static void parse_inline_tag_list(Note *note, const char *value, int is_aliases)
{
    char buf[MAX_LINE + 1];
    char *token;

    copy_string(buf, sizeof(buf), value);
    token = strtok(buf, ",[]");
    while (token) {
        if (is_aliases)
            note_add_alias(note, token);
        else
            note_add_tag(note, token);
        token = strtok(NULL, ",[]");
    }
}

static void parse_inline_content_tags(Note *note, const char *line)
{
    int i = 0;
    int start;
    char tag[MAX_TITLE];
    int out_len;

    while (line[i]) {
        if (line[i] == '#'
            && (i == 0 || isspace((unsigned char)line[i - 1]) || line[i - 1] == '(')) {
            start = i + 1;
            out_len = 0;
            while (line[start] && out_len + 1 < (int)sizeof(tag)
                   && (isalnum((unsigned char)line[start])
                       || line[start] == '-' || line[start] == '_'
                       || line[start] == '/')) {
                tag[out_len++] = line[start++];
            }
            tag[out_len] = '\0';
            if (out_len > 0)
                note_add_tag(note, tag);
            i = start;
            continue;
        }
        i++;
    }
}

static void parse_frontmatter(FILE *fp, Note *note)
{
    char buf[MAX_LINE + 2];
    long pos;
    char section[16];

    section[0] = '\0';
    pos = ftell(fp);
    if (!fgets(buf, sizeof(buf), fp)) {
        fseek(fp, pos, SEEK_SET);
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    if (strcmp(buf, "---") != 0) {
        fseek(fp, pos, SEEK_SET);
        return;
    }
    while (fgets(buf, sizeof(buf), fp)) {
        char *colon;
        char key[MAX_TITLE];
        char value[MAX_LINE + 1];
        char *p = buf;

        buf[strcspn(buf, "\r\n")] = '\0';
        if (strcmp(buf, "---") == 0)
            return;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '-' && p[1] == ' ') {
            if (strcmp(section, "tags") == 0)
                note_add_tag(note, p + 2);
            else if (strcmp(section, "aliases") == 0)
                note_add_alias(note, p + 2);
            continue;
        }
        section[0] = '\0';
        colon = strchr(p, ':');
        if (!colon)
            continue;
        *colon = '\0';
        trim_copy(p, key, sizeof(key));
        trim_copy(colon + 1, value, sizeof(value));
        if (strcmp(key, "title") == 0 && value[0]) {
            copy_string(note->display_title, sizeof(note->display_title), value);
        } else if (strcmp(key, "tags") == 0) {
            copy_string(section, sizeof(section), "tags");
            if (value[0])
                parse_inline_tag_list(note, value, 0);
        } else if (strcmp(key, "aliases") == 0) {
            copy_string(section, sizeof(section), "aliases");
            if (value[0])
                parse_inline_tag_list(note, value, 1);
        } else if (strcmp(key, "pinned") == 0) {
            note->pinned = case_contains(value, "true")
                || strcmp(value, "1") == 0
                || case_contains(value, "yes");
        }
    }
}

static int note_cmp(const void *a, const void *b)
{
    const Note *na = (const Note *)a;
    const Note *nb = (const Note *)b;
    int dir_cmp = strcmp(na->dir_path, nb->dir_path);

    if (dir_cmp != 0)
        return dir_cmp;
    if (sort_mode == SORT_MTIME) {
        if (na->mtime < nb->mtime)
            return 1;
        if (na->mtime > nb->mtime)
            return -1;
    } else if (sort_mode == SORT_CTIME) {
        if (na->ctime < nb->ctime)
            return 1;
        if (na->ctime > nb->ctime)
            return -1;
    }
    return strcmp(na->display_title, nb->display_title);
}

static char *xstrdup(const char *s)
{
    char *p = (char *)malloc(strlen(s) + 1);
    if (p)
        strcpy(p, s);
    return p;
}

static void free_note_indices(void)
{
    int i;

    for (i = 0; i < MAX_NOTES; i++) {
        free(note_index[i].raw_text);
        free(note_index[i].plain_text);
        note_index[i].raw_text = NULL;
        note_index[i].plain_text = NULL;
        note_index[i].outbound_count = 0;
        note_index[i].backlink_count = 0;
        note_index[i].mention_count = 0;
    }
}

static void append_heap_line(char **text, const char *line)
{
    size_t old_len = 0;
    size_t add_len = strlen(line);
    char *buf;

    if (*text)
        old_len = strlen(*text);
    buf = (char *)realloc(*text, old_len + add_len + 2);
    if (!buf)
        return;
    *text = buf;
    memcpy(buf + old_len, line, add_len);
    buf[old_len + add_len] = '\n';
    buf[old_len + add_len + 1] = '\0';
}

static int tag_cmp(const void *a, const void *b)
{
    const TagInfo *ta = (const TagInfo *)a;
    const TagInfo *tb = (const TagInfo *)b;

    return strcmp(ta->name, tb->name);
}

static int dir_cmp(const void *a, const void *b)
{
    const DirectoryInfo *da = (const DirectoryInfo *)a;
    const DirectoryInfo *db = (const DirectoryInfo *)b;

    if (da->depth != db->depth)
        return da->depth - db->depth;
    return strcmp(da->path, db->path);
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

static void build_sidebar(void);

static void add_recent_note(int note_idx)
{
    int i;

    if (note_idx < 0 || note_idx >= note_count)
        return;
    for (i = 0; i < recent_note_count; i++) {
        if (recent_notes[i] == note_idx) {
            memmove(recent_notes + 1, recent_notes,
                    (size_t)i * sizeof(recent_notes[0]));
            recent_notes[0] = note_idx;
            return;
        }
    }
    if (recent_note_count < MAX_RECENT)
        recent_note_count++;
    memmove(recent_notes + 1, recent_notes,
            (size_t)(recent_note_count - 1) * sizeof(recent_notes[0]));
    recent_notes[0] = note_idx;
}

static void history_push_current(HistoryEntry *stack, int *count)
{
    int line = 0;

    if (current_note < 0 || current_note >= note_count)
        return;
    if (*count >= MAX_HISTORY) {
        memmove(stack, stack + 1, (size_t)(MAX_HISTORY - 1) * sizeof(stack[0]));
        *count = MAX_HISTORY - 1;
    }
    copy_string(stack[*count].title, sizeof(stack[*count].title),
                notes[current_note].title);
    if (read_mode && rendered_line_count > 0 && note_scroll < rendered_line_count)
        line = rendered_lines[note_scroll].source_line;
    else
        line = note_scroll;
    stack[*count].line = line;
    (*count)++;
}

static void clear_forward_history(void)
{
    history_forward_count = 0;
}

static void note_collect_metadata(Note *note)
{
    char path[PATH_MAX];
    FILE *fp;
    char buf[MAX_LINE + 2];
    struct stat st;

    note->tag_count = 0;
    note->alias_count = 0;
    note->pinned = 0;
    copy_string(note->display_title, sizeof(note->display_title), note->title);
    make_path(path, sizeof(path), note->rel_path);
    if (stat(path, &st) == 0) {
        note->mtime = (long)st.st_mtime;
        note->ctime = (long)st.st_ctime;
    } else {
        note->mtime = 0;
        note->ctime = 0;
    }
    fp = fopen(path, "r");
    if (!fp)
        return;
    parse_frontmatter(fp, note);
    while (fgets(buf, sizeof(buf), fp))
        parse_inline_content_tags(note, buf);
    fclose(fp);
}

static void load_note_entry(const char *rel_path)
{
    char file_name[MAX_NAME + 4];
    char dir_path[PATH_MAX];
    Note *note;

    if (note_count >= MAX_NOTES)
        return;
    note = &notes[note_count];
    memset(note, 0, sizeof(*note));
    copy_string(note->rel_path, sizeof(note->rel_path), rel_path);
    split_rel_path(rel_path, dir_path, sizeof(dir_path), file_name, sizeof(file_name));
    copy_string(note->dir_path, sizeof(note->dir_path), dir_path);
    copy_string(note->file, sizeof(note->file), rel_path);
    strip_md_suffix(file_name, note->title, sizeof(note->title));
    copy_string(note->display_title, sizeof(note->display_title), note->title);
    if (dir_path[0])
        ensure_dir_entry(dir_path);
    note_collect_metadata(note);
    note_count++;
}

static void scan_notes_recursive(const char *rel_dir)
{
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *ent;

    if (rel_dir[0]) {
        make_path(path, sizeof(path), rel_dir);
    } else {
        copy_string(path, sizeof(path), note_dir);
    }
    dir = opendir(path);
    if (!dir)
        return;
    while ((ent = readdir(dir)) != NULL && note_count < MAX_NOTES) {
        char child_rel[PATH_MAX];
        char child_path[PATH_MAX];
        struct stat st;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strcmp(ent->d_name, trash_dir_name) == 0
            || strcmp(ent->d_name, template_dir_name) == 0)
            continue;
        if (ent->d_name[0] == '.' && !has_md_suffix(ent->d_name))
            continue;
        child_rel[0] = '\0';
        if (rel_dir[0]) {
            copy_string(child_rel, sizeof(child_rel), rel_dir);
            append_string(child_rel, sizeof(child_rel), "/");
        }
        append_string(child_rel, sizeof(child_rel), ent->d_name);
        make_path(child_path, sizeof(child_path), child_rel);
        if (stat(child_path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            ensure_dir_entry(child_rel);
            scan_notes_recursive(child_rel);
        } else if (has_md_suffix(ent->d_name)) {
            load_note_entry(child_rel);
        }
    }
    closedir(dir);
}

static int note_matches_current_filters(int note_idx)
{
    return note_visible_in_sidebar(note_idx);
}

static void sidebar_add_item(int kind, int note_index, int dir_index,
                             const char *label, int depth)
{
    if (sidebar_item_count >= MAX_SIDEBAR_ITEMS)
        return;
    sidebar_items[sidebar_item_count].kind = kind;
    sidebar_items[sidebar_item_count].note_index = note_index;
    sidebar_items[sidebar_item_count].dir_index = dir_index;
    copy_string(sidebar_items[sidebar_item_count].label,
                sizeof(sidebar_items[sidebar_item_count].label), label);
    sidebar_items[sidebar_item_count].depth = depth;
    sidebar_item_count++;
}

static void build_sidebar(void)
{
    int i;
    char label[MAX_LINE + 1];

    sidebar_item_count = 0;
    for (i = 0; i < note_count && sidebar_item_count < MAX_SIDEBAR_ITEMS; i++) {
        if (notes[i].pinned && note_matches_filter(i)) {
            if (sidebar_item_count == 0
                || strcmp(sidebar_items[sidebar_item_count - 1].label, "[Pinned]") != 0)
                sidebar_add_item(SIDEBAR_KIND_SECTION, -1, -1, "[Pinned]", 0);
            sprintf(label, "* %s", notes[i].display_title);
            sidebar_add_item(SIDEBAR_KIND_NOTE, i, -1, label, 1);
        }
    }
    if (recent_note_count > 0) {
        sidebar_add_item(SIDEBAR_KIND_SECTION, -1, -1, "[Recent]", 0);
        for (i = 0; i < recent_note_count && sidebar_item_count < MAX_SIDEBAR_ITEMS; i++) {
            int note_idx = recent_notes[i];

            if (note_idx >= 0 && note_idx < note_count && note_matches_filter(note_idx)) {
                sprintf(label, "%s", notes[note_idx].display_title);
                sidebar_add_item(SIDEBAR_KIND_NOTE, note_idx, -1, label, 1);
            }
        }
    }
    for (i = 0; i < dir_count && sidebar_item_count < MAX_SIDEBAR_ITEMS; i++) {
        int has_note = 0;
        int j;

        if (!parent_dir_visible(dirs[i].path))
            continue;
        for (j = 0; j < note_count; j++) {
            if ((strcmp(notes[j].dir_path, dirs[i].path) == 0
                 || (strncmp(notes[j].dir_path, dirs[i].path, strlen(dirs[i].path)) == 0
                     && notes[j].dir_path[strlen(dirs[i].path)] == '/'))
                && note_matches_filter(j)) {
                has_note = 1;
                break;
            }
        }
        if (!has_note)
            continue;
        sprintf(label, "%c %s", dirs[i].expanded ? '-' : '+', dirs[i].name);
        sidebar_add_item(SIDEBAR_KIND_DIR, -1, i, label, dirs[i].depth);
        if (!dirs[i].expanded)
            continue;
        for (j = 0; j < note_count && sidebar_item_count < MAX_SIDEBAR_ITEMS; j++) {
            if (strcmp(notes[j].dir_path, dirs[i].path) == 0
                && note_matches_filter(j)) {
                sprintf(label, "%s", notes[j].display_title);
                sidebar_add_item(SIDEBAR_KIND_NOTE, j, -1, label, dirs[i].depth + 1);
            }
        }
    }
    for (i = 0; i < note_count && sidebar_item_count < MAX_SIDEBAR_ITEMS; i++) {
        if (notes[i].dir_path[0] == '\0' && note_matches_filter(i))
            sidebar_add_item(SIDEBAR_KIND_NOTE, i, -1, notes[i].display_title, 0);
    }
    if (selected_note >= sidebar_item_count)
        selected_note = sidebar_item_count > 0 ? sidebar_item_count - 1 : 0;
}

static void load_notes(void)
{
    int keep_current = current_note;
    char current_title[MAX_TITLE];
    char recent_titles[MAX_RECENT][MAX_TITLE];
    int old_recent_count = recent_note_count;
    int r;

    current_title[0] = '\0';
    if (keep_current >= 0 && keep_current < note_count)
        copy_string(current_title, sizeof(current_title),
                    notes[keep_current].title);
    for (r = 0; r < old_recent_count && r < MAX_RECENT; r++) {
        if (recent_notes[r] >= 0 && recent_notes[r] < note_count)
            copy_string(recent_titles[r], sizeof(recent_titles[r]),
                        notes[recent_notes[r]].title);
        else
            recent_titles[r][0] = '\0';
    }

    note_count = 0;
    dir_count = 0;
    clear_tag_cache();
    {
        DIR *dir = opendir(note_dir);
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
        closedir(dir);
    }

    scan_notes_recursive("");
    qsort(notes, note_count, sizeof(Note), note_cmp);
    qsort(dirs, dir_count, sizeof(DirectoryInfo), dir_cmp);
    for (keep_current = 0; keep_current < note_count; keep_current++) {
        int j;

        for (j = 0; j < notes[keep_current].tag_count; j++)
            add_global_tag(notes[keep_current].tags[j]);
    }
    qsort(tags, tag_count, sizeof(TagInfo), tag_cmp);

    current_note = -1;
    selected_note = 0;
    if (current_title[0]) {
        int i;

        for (i = 0; i < note_count; i++) {
            if (strcmp(current_title, notes[i].title) == 0) {
                current_note = i;
                break;
            }
        }
    }
    build_sidebar();
    recent_note_count = 0;
    for (r = 0; r < old_recent_count && r < MAX_RECENT; r++) {
        int i = find_note_by_target(recent_titles[r]);

        if (i >= 0)
            recent_notes[recent_note_count++] = i;
    }
    build_sidebar();
    build_note_indices();
    if (current_note >= 0)
        selected_note = visible_position_for_note(current_note);
}

static void set_keybinding(const char *name, const char *value)
{
    int key = value[0];

    if (case_equals(value, "space"))
        key = ' ';
    else if (case_equals(value, "slash"))
        key = '/';
    else if (case_equals(value, "tab"))
        key = '\t';
    if (strcmp(name, "key_new") == 0)
        key_new_note = key;
    else if (strcmp(name, "key_template") == 0)
        key_template_note = key;
    else if (strcmp(name, "key_daily") == 0)
        key_daily_note = key;
    else if (strcmp(name, "key_edit") == 0)
        key_edit_note = key;
    else if (strcmp(name, "key_delete") == 0)
        key_delete_note = key;
    else if (strcmp(name, "key_rename") == 0)
        key_rename_note = key;
    else if (strcmp(name, "key_filter") == 0)
        key_filter_titles = key;
    else if (strcmp(name, "key_search") == 0)
        key_full_text_search = key;
    else if (strcmp(name, "key_backlinks") == 0)
        key_backlinks = key;
    else if (strcmp(name, "key_mentions") == 0)
        key_mentions = key;
    else if (strcmp(name, "key_outline") == 0)
        key_outline = key;
    else if (strcmp(name, "key_tags") == 0)
        key_tags = key;
    else if (strcmp(name, "key_saved") == 0)
        key_saved_searches = key;
    else if (strcmp(name, "key_palette") == 0)
        key_command_palette = key;
    else if (strcmp(name, "key_info") == 0)
        key_note_info = key;
    else if (strcmp(name, "key_theme") == 0)
        key_cycle_theme = key;
}

static void load_config(void)
{
    char path[PATH_MAX];
    FILE *fp;
    char buf[MAX_LINE + 2];

    make_special_path(path, sizeof(path), CONFIG_FILE);
    fp = fopen(path, "r");
    if (!fp)
        return;
    while (fgets(buf, sizeof(buf), fp)) {
        char key[MAX_TITLE];
        char value[MAX_LINE + 1];
        char *eq;

        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '#' || buf[0] == '\0')
            continue;
        eq = strchr(buf, '=');
        if (!eq)
            continue;
        *eq = '\0';
        trim_copy(buf, key, sizeof(key));
        trim_copy(eq + 1, value, sizeof(value));
        if (strcmp(key, "theme") == 0)
            copy_string(theme_name, sizeof(theme_name), value);
        else if (strcmp(key, "default_mode") == 0)
            read_mode = case_equals(value, "write") ? 0 : 1;
        else if (strcmp(key, "startup") == 0)
            copy_string(startup_mode, sizeof(startup_mode), value);
        else if (strcmp(key, "trash_dir") == 0 && value[0])
            copy_string(trash_dir_name, sizeof(trash_dir_name), value);
        else if (strcmp(key, "template_dir") == 0 && value[0])
            copy_string(template_dir_name, sizeof(template_dir_name), value);
        else
            set_keybinding(key, value);
    }
    fclose(fp);
}

static void load_saved_searches(void)
{
    char path[PATH_MAX];
    FILE *fp;
    char buf[MAX_LINE + 2];

    saved_search_count = 0;
    make_special_path(path, sizeof(path), SAVED_SEARCH_FILE);
    fp = fopen(path, "r");
    if (!fp)
        return;
    while (fgets(buf, sizeof(buf), fp) && saved_search_count < MAX_SAVED_SEARCHES) {
        char *sep;

        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '\0')
            continue;
        sep = strchr(buf, '|');
        if (!sep)
            continue;
        *sep = '\0';
        trim_copy(buf, saved_searches[saved_search_count].name,
                  sizeof(saved_searches[saved_search_count].name));
        trim_copy(sep + 1, saved_searches[saved_search_count].query,
                  sizeof(saved_searches[saved_search_count].query));
        if (saved_searches[saved_search_count].name[0]
            && saved_searches[saved_search_count].query[0])
            saved_search_count++;
    }
    fclose(fp);
}

static void save_saved_searches(void)
{
    char path[PATH_MAX];
    FILE *fp;
    int i;

    make_special_path(path, sizeof(path), SAVED_SEARCH_FILE);
    fp = fopen(path, "w");
    if (!fp)
        return;
    for (i = 0; i < saved_search_count; i++)
        fprintf(fp, "%s|%s\n", saved_searches[i].name, saved_searches[i].query);
    fclose(fp);
}

static void note_index_add_unique(int *items, int *count, int max_count, int value)
{
    int i;

    for (i = 0; i < *count; i++) {
        if (items[i] == value)
            return;
    }
    if (*count >= max_count)
        return;
    items[(*count)++] = value;
}

static void build_note_indices(void)
{
    int i, j;

    free_note_indices();
    for (i = 0; i < note_count; i++) {
        FILE *fp;
        char path[PATH_MAX];
        char buf[MAX_LINE + 2];

        make_path(path, sizeof(path), notes[i].file);
        fp = fopen(path, "r");
        if (!fp)
            continue;
        while (fgets(buf, sizeof(buf), fp)) {
            const char *p = buf;
            const char *end;
            char raw[MAX_TITLE * 2];
            char target[MAX_TITLE];
            char heading[MAX_TITLE];
            char label[MAX_TITLE];
            char plain[MAX_LINE + 1];
            int len;

            buf[strcspn(buf, "\r\n")] = '\0';
            append_heap_line(&note_index[i].raw_text, buf);
            format_inline_markdown(buf, plain, sizeof(plain));
            append_heap_line(&note_index[i].plain_text, plain[0] ? plain : buf);
            while ((p = strstr(p, "[[")) != NULL) {
                if (p > buf && p[-1] == '!')
                    p += 2;
                else
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
                j = find_note_by_target(target);
                if (j >= 0 && j != i)
                    note_index_add_unique(note_index[i].outbound,
                                          &note_index[i].outbound_count,
                                          MAX_LINKS, j);
                p = end + 2;
            }
        }
        fclose(fp);
    }

    for (i = 0; i < note_count; i++) {
        for (j = 0; j < note_index[i].outbound_count; j++) {
            int target = note_index[i].outbound[j];

            if (target >= 0 && target < note_count)
                note_index_add_unique(note_index[target].backlinks,
                                      &note_index[target].backlink_count,
                                      MAX_BACKLINKS, i);
        }
    }

    for (i = 0; i < note_count; i++) {
        for (j = 0; j < note_count; j++) {
            int linked = 0;
            int k;

            if (i == j || notes[j].title[0] == '\0')
                continue;
            if (!text_contains_phrase(note_index[i].plain_text ? note_index[i].plain_text : "",
                                      notes[j].title))
                continue;
            for (k = 0; k < note_index[i].outbound_count; k++) {
                if (note_index[i].outbound[k] == j) {
                    linked = 1;
                    break;
                }
            }
            if (!linked)
                note_index_add_unique(note_index[i].mentions,
                                      &note_index[i].mention_count,
                                      MAX_MENTIONS, j);
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
    fprintf(fp, "sort_mode=%d\n", sort_mode);
    fprintf(fp, "theme=%s\n", theme_name);
    fprintf(fp, "active_tag=%s\n", active_tag);
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
        } else if (strncmp(buf, "sort_mode=", 10) == 0) {
            sort_mode = atoi(buf + 10);
        } else if (strncmp(buf, "theme=", 6) == 0) {
            copy_string(theme_name, sizeof(theme_name), buf + 6);
        } else if (strncmp(buf, "active_tag=", 11) == 0) {
            copy_string(active_tag, sizeof(active_tag), buf + 11);
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

static int parse_embed_target(const char *line, char *target, size_t target_size)
{
    const char *p = line;
    const char *end;
    char raw[MAX_TITLE * 2];
    char heading[MAX_TITLE];
    char label[MAX_TITLE];
    int len;

    while (*p == ' ' || *p == '\t')
        p++;
    if (p[0] != '!' || p[1] != '[' || p[2] != '[')
        return 0;
    p += 3;
    end = strstr(p, "]]");
    if (!end)
        return 0;
    len = (int)(end - p);
    if (len >= (int)sizeof(raw))
        len = (int)sizeof(raw) - 1;
    memcpy(raw, p, (size_t)len);
    raw[len] = '\0';
    parse_wiki_link(raw, target, target_size, heading, sizeof(heading),
                    label, sizeof(label));
    return target[0] != '\0';
}

static int is_table_divider(const char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    while (*line) {
        if (*line != '|' && *line != '-' && *line != ':' && *line != ' ')
            return 0;
        line++;
    }
    return 1;
}

static void render_table_line(const char *line, int source_line, int link_index, int width)
{
    char formatted[MAX_LINE + 1];
    char cell[MAX_LINE + 1];
    const char *p = line;
    const char *start;

    formatted[0] = '\0';
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '|')
            p++;
        start = p;
        while (*p && *p != '|')
            p++;
        if (p > start) {
            memcpy(cell, start, (size_t)(p - start));
            cell[p - start] = '\0';
            trim_copy(cell, cell, sizeof(cell));
            if (formatted[0])
                append_string(formatted, sizeof(formatted), " | ");
            append_string(formatted, sizeof(formatted), cell);
        }
    }
    add_wrapped_prefixed_line("| ", formatted, 2, A_BOLD, source_line, link_index, width);
}

static void render_embed_note(const char *target, int source_line, int link_index, int width)
{
    int idx = find_note_by_target(target);
    char path[PATH_MAX];
    FILE *fp;
    char buf[MAX_LINE + 2];
    char plain[MAX_LINE + 1];
    int shown = 0;

    add_wrapped_prefixed_line("> ", target, 2, A_BOLD, source_line, link_index, width);
    if (idx < 0) {
        add_wrapped_prefixed_line("  ", "(missing embed)", 2, A_DIM, source_line,
                                  link_index, width);
        return;
    }
    make_path(path, sizeof(path), notes[idx].file);
    fp = fopen(path, "r");
    if (!fp)
        return;
    while (fgets(buf, sizeof(buf), fp) && shown < 12) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (is_blank_line(buf))
            continue;
        format_inline_markdown(buf, plain, sizeof(plain));
        add_wrapped_prefixed_line("  ", plain[0] ? plain : buf, 2, A_DIM,
                                  source_line, link_index, width);
        shown++;
    }
    fclose(fp);
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
    char embed_target[MAX_TITLE];

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
        if (strncmp(p, "```", 3) == 0) {
            in_code_block = !in_code_block;
            add_wrapped_prefixed_line("", p, 0, A_DIM, i, line_link, width);
            continue;
        }
        if (in_code_block) {
            add_wrapped_prefixed_line("  ", view_lines[i], 2, A_DIM, i,
                                      line_link, width);
            continue;
        }
        if (parse_embed_target(view_lines[i], embed_target, sizeof(embed_target))) {
            render_embed_note(embed_target, i, line_link, width);
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
        if (strchr(view_lines[i], '|') && !is_table_divider(view_lines[i])) {
            if (i + 1 < view_line_count && is_table_divider(view_lines[i + 1])) {
                render_table_line(view_lines[i], i, line_link, width);
                continue;
            }
            if (i > 0 && strchr(view_lines[i - 1], '|') && is_table_divider(view_lines[i - 1])) {
                render_table_line(view_lines[i], i, line_link, width);
                continue;
            }
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
            if (p[n + 1] == ' ' && p[n + 2] == '[') {
                const char *callout = strchr(p + n + 2, ']');

                if (callout && callout[1] == ':') {
                    format_inline_markdown(callout + 2, text, sizeof(text));
                    add_wrapped_prefixed_line("! ", text, 2, A_BOLD, i,
                                              line_link, width);
                    continue;
                }
            }
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
    add_recent_note(idx);
    build_sidebar();
    selected_note = visible_position_for_note(idx);
    save_state();
}

static int find_note_by_target(const char *target)
{
    int i;

    for (i = 0; i < note_count; i++) {
        int j;

        if (strcmp(notes[i].title, target) == 0
            || strcmp(notes[i].display_title, target) == 0)
            return i;
        for (j = 0; j < notes[i].alias_count; j++) {
            if (strcmp(notes[i].aliases[j], target) == 0)
                return i;
        }
    }
    return -1;
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

static void sanitize_rel_title(const char *src, char *dst, size_t dst_size)
{
    char segment[MAX_TITLE];
    char clean_segment[MAX_TITLE];
    int seg_len = 0;
    size_t i;

    dst[0] = '\0';
    for (i = 0; src[i] && strlen(dst) + 1 < dst_size; i++) {
        if (src[i] == '/') {
            segment[seg_len] = '\0';
            sanitize_title(segment, clean_segment, sizeof(clean_segment));
            if (clean_segment[0]) {
                if (dst[0])
                    append_string(dst, dst_size, "/");
                append_string(dst, dst_size, clean_segment);
            }
            seg_len = 0;
        } else if (seg_len + 1 < (int)sizeof(segment)) {
            segment[seg_len++] = src[i];
        }
    }
    segment[seg_len] = '\0';
    sanitize_title(segment, clean_segment, sizeof(clean_segment));
    if (clean_segment[0]) {
        if (dst[0])
            append_string(dst, dst_size, "/");
        append_string(dst, dst_size, clean_segment);
    }
}

static int ensure_parent_dirs(const char *rel_path)
{
    char full[PATH_MAX];
    char *p;

    make_path(full, sizeof(full), rel_path);
    for (p = full + strlen(note_dir) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(full, 0777) != 0 && errno != EEXIST)
                return 0;
            *p = '/';
        }
    }
    return 1;
}

static void make_template_path(char *out, size_t out_size, const char *name)
{
    copy_string(out, out_size, note_dir);
    append_string(out, out_size, "/");
    append_string(out, out_size, template_dir_name);
    append_string(out, out_size, "/");
    append_string(out, out_size, name);
}

static void write_note_template(FILE *out, const char *title, const char *template_path)
{
    FILE *in = fopen(template_path, "r");
    char buf[MAX_LINE + 2];

    if (in) {
        while (fgets(buf, sizeof(buf), in)) {
            char *pos = strstr(buf, "{{title}}");

            if (pos) {
                *pos = '\0';
                fputs(buf, out);
                fputs(title, out);
                fputs(pos + 9, out);
            } else {
                fputs(buf, out);
            }
        }
        fclose(in);
    } else {
        fprintf(out, "# %s\n\n", title);
    }
}

static int create_note_with_template(const char *title, const char *template_name)
{
    char clean[PATH_MAX];
    char rel_path[PATH_MAX];
    char path[PATH_MAX];
    char template_path[PATH_MAX];
    char leaf[MAX_TITLE];
    FILE *fp;

    sanitize_rel_title(title, clean, sizeof(clean));
    if (clean[0] == '\0')
        copy_string(clean, sizeof(clean), "Untitled");
    copy_string(rel_path, sizeof(rel_path), clean);
    append_string(rel_path, sizeof(rel_path), ".md");
    make_path(path, sizeof(path), rel_path);

    fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        set_status("Note already exists");
        return 0;
    }
    if (!ensure_parent_dirs(rel_path)) {
        set_status("Could not create parent folders");
        return 0;
    }

    fp = fopen(path, "w");
    if (!fp) {
        set_status("Could not create note");
        return 0;
    }
    strip_md_suffix(path_basename(rel_path), leaf, sizeof(leaf));
    make_template_path(template_path, sizeof(template_path),
                       template_name ? template_name : DEFAULT_TEMPLATE);
    write_note_template(fp, leaf, template_path);
    fclose(fp);
    load_notes();
    set_status("Note created");
    return 1;
}

static int create_note(const char *title)
{
    return create_note_with_template(title, DEFAULT_TEMPLATE);
}

static void load_daily_format(char *out, size_t out_size)
{
    char path[PATH_MAX];
    FILE *fp;

    copy_string(out, out_size, "Daily/%Y-%m-%d");
    make_special_path(path, sizeof(path), DAILY_FORMAT_FILE);
    fp = fopen(path, "r");
    if (!fp)
        return;
    if (fgets(out, (int)out_size, fp))
        out[strcspn(out, "\r\n")] = '\0';
    fclose(fp);
}

static void open_daily_note(void)
{
    char format[MAX_TITLE];
    char rel_title[PATH_MAX];
    char leaf[MAX_TITLE];
    time_t now;
    struct tm *tm_now;
    int idx;

    load_daily_format(format, sizeof(format));
    now = time(NULL);
    tm_now = localtime(&now);
    if (!tm_now)
        return;
    strftime(rel_title, sizeof(rel_title), format, tm_now);
    strip_md_suffix(path_basename(rel_title), leaf, sizeof(leaf));
    idx = find_note_by_target(leaf);
    if (idx >= 0) {
        load_note_view(idx);
        set_status("Opened daily note");
        return;
    }
    if (create_note_with_template(rel_title, DAILY_TEMPLATE)) {
        idx = find_note_by_target(leaf);
        if (idx >= 0)
            load_note_view(idx);
        set_status("Created daily note");
    }
}

static int ensure_trash_dir(void)
{
    char path[PATH_MAX];
    struct stat st;

    make_special_path(path, sizeof(path), trash_dir_name);
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

    make_special_path(dir_path, sizeof(dir_path), trash_dir_name);
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

static void rewrite_links_recursive(const char *rel_dir, const char *old_title,
                                    const char *new_title)
{
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *ent;

    if (rel_dir[0])
        make_path(path, sizeof(path), rel_dir);
    else
        copy_string(path, sizeof(path), note_dir);
    dir = opendir(path);
    if (!dir)
        return;
    while ((ent = readdir(dir)) != NULL) {
        char child_rel[PATH_MAX];
        char child_path[PATH_MAX];
        struct stat st;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        child_rel[0] = '\0';
        if (rel_dir[0]) {
            copy_string(child_rel, sizeof(child_rel), rel_dir);
            append_string(child_rel, sizeof(child_rel), "/");
        }
        append_string(child_rel, sizeof(child_rel), ent->d_name);
        make_path(child_path, sizeof(child_path), child_rel);
        if (stat(child_path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (strcmp(ent->d_name, trash_dir_name) != 0
                && strcmp(ent->d_name, template_dir_name) != 0)
                rewrite_links_recursive(child_rel, old_title, new_title);
        } else if (has_md_suffix(ent->d_name)) {
            rewrite_file_links(child_rel, old_title, new_title);
        }
    }
    closedir(dir);
}

static void rewrite_links_for_rename(const char *old_title, const char *new_title)
{
    rewrite_links_recursive("", old_title, new_title);
}

static void rename_current_note(void)
{
    char title[PATH_MAX];
    char clean[PATH_MAX];
    char file[PATH_MAX];
    char old_path[PATH_MAX], new_path[PATH_MAX];
    char old_title[MAX_TITLE];
    FILE *fp;

    if (current_note < 0)
        return;
    copy_string(title, sizeof(title), notes[current_note].rel_path);
    if (has_md_suffix(title))
        title[strlen(title) - 3] = '\0';
    if (!prompt_text("Rename: ", title, sizeof(title)))
        return;
    sanitize_rel_title(title, clean, sizeof(clean));
    copy_string(file, sizeof(file), clean);
    append_string(file, sizeof(file), ".md");
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
    if (!ensure_parent_dirs(file)) {
        set_status("Could not create target folders");
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
    char title[MAX_TITLE];

    if (current_note < 0)
        return 0;
    copy_string(title, sizeof(title), notes[current_note].title);
    make_path(path, sizeof(path), notes[current_note].file);
    fp = fopen(path, "w");
    if (!fp) {
        set_status("Could not save note");
        return 0;
    }
    for (i = 0; i < edit_line_count; i++)
        fprintf(fp, "%s\n", edit_lines[i]);
    fclose(fp);
    edit_dirty = 0;
    load_notes();
    i = find_note_by_target(title);
    if (i >= 0)
        load_note_view(i);
    set_status("Saved");
    return 1;
}

static void draw_header(void)
{
    attron(A_REVERSE);
    move(0, 0);
    clrtoeol();
    printw(" memex  mode:%s  view:%s  sort:%s  theme:%s  tag:%s  /:%s",
           read_mode ? "read" : "write",
           current_panel == PANEL_NOTE ? "note" :
           current_panel == PANEL_BACKLINKS ? "backlinks" :
           current_panel == PANEL_SEARCH ? "search" :
           current_panel == PANEL_OUTLINE ? "outline" :
           current_panel == PANEL_TAGS ? "tags" :
           current_panel == PANEL_MENTIONS ? "mentions" :
           current_panel == PANEL_INFO ? "info" :
           current_panel == PANEL_SAVED ? "saved" : "palette",
           sort_mode == SORT_ALPHA ? "alpha" :
           sort_mode == SORT_MTIME ? "mtime" : "ctime",
           theme_name,
           active_tag[0] ? active_tag : "-",
           note_filter[0] ? note_filter : "-");
    attroff(A_REVERSE);
}

static void draw_status(void)
{
    const char *msg = status_msg[0] ? status_msg :
        "p palette  f search  b backlinks  u mentions  v saved  i info  Enter open/follow  e edit  q quit";

    attron(A_REVERSE);
    move(LINES - 1, 0);
    clrtoeol();
    printw(" %s", msg);
    attroff(A_REVERSE);
}

static void draw_notes(int width)
{
    int y, idx, item_idx, i;
    SidebarItem *item;

    build_sidebar();
    if (selected_note >= sidebar_item_count)
        selected_note = sidebar_item_count > 0 ? sidebar_item_count - 1 : 0;
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
        item_idx = top_note + y - 2;
        if (item_idx < 0 || item_idx >= sidebar_item_count)
            continue;
        item = &sidebar_items[item_idx];
        if (item_idx == selected_note)
            attron(A_REVERSE);
        for (i = 0; i < item->depth * 2 && i < width - 1; i++)
            mvaddch(y, i, ' ');
        idx = item->depth * 2;
        if (idx > width - 2)
            idx = width - 2;
        if (idx < 0)
            idx = 0;
        mvprintw(y, idx, "%-*.*s", width - idx - 1, width - idx - 1, item->label);
        if (item_idx == selected_note)
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

    mvprintw(1, x, "%s [%s]  %s", notes[current_note].display_title,
             read_mode ? "read" : "write", notes[current_note].rel_path);

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

static void build_backlinks(void)
{
    int i;

    backlink_count = 0;
    if (current_note < 0)
        return;
    for (i = 0; i < note_index[current_note].backlink_count && i < MAX_BACKLINKS; i++)
        backlink_indices[backlink_count++] = note_index[current_note].backlinks[i];
    panel_selected = 0;
    panel_scroll = 0;
    current_panel = PANEL_BACKLINKS;
}

static void run_full_text_search(const char *query)
{
    int i;
    copy_string(current_search_query, sizeof(current_search_query), query);

    search_result_count = 0;
    for (i = 0; i < note_count && search_result_count < MAX_RESULTS; i++) {
        FILE *fp;
        char path[PATH_MAX];
        char buf[MAX_LINE + 2];
        char plain[MAX_LINE + 1];
        int line_no;

        if (!case_contains(note_index[i].plain_text ? note_index[i].plain_text : "", query)
            && !case_contains(note_index[i].raw_text ? note_index[i].raw_text : "", query))
            continue;
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

static void build_mentions(void)
{
    int i;

    mention_count = 0;
    if (current_note < 0)
        return;
    for (i = 0; i < note_index[current_note].mention_count && i < MAX_MENTIONS; i++)
        mention_indices[mention_count++] = note_index[current_note].mentions[i];
    if (mention_count == 0)
        set_status("No unlinked mentions");
    panel_selected = 0;
    panel_scroll = 0;
    current_panel = PANEL_MENTIONS;
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

static void open_tags(void)
{
    if (tag_count == 0) {
        set_status("No tags found");
        return;
    }
    panel_selected = selected_tag;
    panel_scroll = 0;
    current_panel = PANEL_TAGS;
}

static void build_note_info(void)
{
    char buf[MAX_LINE + 1];
    int i;
    int line = 0;
    struct tm *tm_info;
    time_t when;

    info_line_count = 0;
    if (current_note < 0) {
        current_panel = PANEL_INFO;
        return;
    }

    sprintf(info_lines[line++], "Title: %s", notes[current_note].title);
    sprintf(info_lines[line++], "Display: %s", notes[current_note].display_title);
    sprintf(info_lines[line++], "Path: %s", notes[current_note].rel_path);
    if (notes[current_note].tag_count > 0) {
        buf[0] = '\0';
        append_string(buf, sizeof(buf), "Tags: ");
        for (i = 0; i < notes[current_note].tag_count; i++) {
            if (i > 0)
                append_string(buf, sizeof(buf), ", ");
            append_string(buf, sizeof(buf), notes[current_note].tags[i]);
        }
        copy_string(info_lines[line++], sizeof(info_lines[0]), buf);
    }
    if (notes[current_note].alias_count > 0) {
        buf[0] = '\0';
        append_string(buf, sizeof(buf), "Aliases: ");
        for (i = 0; i < notes[current_note].alias_count; i++) {
            if (i > 0)
                append_string(buf, sizeof(buf), ", ");
            append_string(buf, sizeof(buf), notes[current_note].aliases[i]);
        }
        copy_string(info_lines[line++], sizeof(info_lines[0]), buf);
    }
    sprintf(info_lines[line++], "Links out: %d", note_index[current_note].outbound_count);
    sprintf(info_lines[line++], "Backlinks: %d", note_index[current_note].backlink_count);
    sprintf(info_lines[line++], "Unlinked mentions: %d", note_index[current_note].mention_count);
    when = (time_t)notes[current_note].mtime;
    tm_info = localtime(&when);
    if (tm_info && line < MAX_INFO_LINES) {
        strftime(buf, sizeof(buf), "Modified: %Y-%m-%d %H:%M", tm_info);
        copy_string(info_lines[line++], sizeof(info_lines[0]), buf);
    }
    info_line_count = line;
    panel_selected = 0;
    panel_scroll = 0;
    current_panel = PANEL_INFO;
}

static void open_saved_searches(void)
{
    if (saved_search_count == 0) {
        set_status("No saved searches");
        return;
    }
    panel_selected = selected_saved_search;
    panel_scroll = 0;
    current_panel = PANEL_SAVED;
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

static void draw_tags(int x, int width)
{
    int y;
    int idx;
    char line[MAX_LINE + 1];

    mvprintw(1, x, "Tags");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= tag_count)
            continue;
        sprintf(line, "#%s (%d)%s", tags[idx].name, tags[idx].count,
                strcmp(active_tag, tags[idx].name) == 0 ? " *" : "");
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, line);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_mentions(int x, int width)
{
    int i, y, idx;

    mvprintw(1, x, "Unlinked mentions");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= mention_count)
            continue;
        i = mention_indices[idx];
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, notes[i].title);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_note_info(int x, int width)
{
    int y, idx;

    mvprintw(1, x, "Note info");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= info_line_count)
            continue;
        mvprintw(y, x, "%-*.*s", width, width, info_lines[idx]);
    }
}

static void draw_saved_searches(int x, int width)
{
    int y, idx;
    char line[MAX_LINE + 1];

    mvprintw(1, x, "Saved searches");
    for (y = 2; y < LINES - 1; y++) {
        move(y, x);
        clrtoeol();
        idx = panel_scroll + y - 2;
        if (idx >= saved_search_count)
            continue;
        sprintf(line, "%s  [%s]", saved_searches[idx].name, saved_searches[idx].query);
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, line);
        if (idx == panel_selected)
            attroff(A_REVERSE);
    }
}

static void draw_commands(int x, int width)
{
    int y, idx;
    static CommandEntry commands[MAX_COMMANDS] = {
        {"New note", 1},
        {"New note from template", 2},
        {"Open daily note", 3},
        {"Edit current note", 4},
        {"Find in notes", 5},
        {"Show backlinks", 6},
        {"Show unlinked mentions", 7},
        {"Show outline", 8},
        {"Show tags", 9},
        {"Show saved searches", 10},
        {"Show note info", 11},
        {"Toggle read/write mode", 12},
        {"Toggle sidebar", 13},
        {"Cycle sort mode", 14},
        {"Cycle color scheme", 15},
        {"Rename note", 16},
        {"Trash note", 17}
    };

    mvprintw(1, x, "Command palette");
    for (y = 2; y < LINES - 1; y++) {
        idx = panel_scroll + y - 2;
        move(y, x);
        clrtoeol();
        if (idx >= 17)
            continue;
        if (idx == panel_selected)
            attron(A_REVERSE);
        mvprintw(y, x, "%-*.*s", width, width, commands[idx].label);
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
    else if (current_panel == PANEL_TAGS)
        draw_tags(x, COLS - x - 1);
    else if (current_panel == PANEL_MENTIONS)
        draw_mentions(x, COLS - x - 1);
    else if (current_panel == PANEL_INFO)
        draw_note_info(x, COLS - x - 1);
    else if (current_panel == PANEL_SAVED)
        draw_saved_searches(x, COLS - x - 1);
    else if (current_panel == PANEL_COMMANDS)
        draw_commands(x, COLS - x - 1);
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
    mvprintw(0, 0, " editing %s%s  Ctrl-X save  Esc discard  Ctrl-Z undo  Ctrl-Y redo  Ctrl-T checkbox  Ctrl-F find",
             current_note >= 0 ? notes[current_note].title : "",
             edit_dirty ? " *" : "");
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

static char *editor_serialize(void)
{
    size_t total = 1;
    int i;
    char *buf;

    for (i = 0; i < edit_line_count; i++)
        total += strlen(edit_lines[i]) + 1;
    buf = (char *)malloc(total);
    if (!buf)
        return NULL;
    buf[0] = '\0';
    for (i = 0; i < edit_line_count; i++) {
        append_string(buf, total, edit_lines[i]);
        append_string(buf, total, "\n");
    }
    return buf;
}

static void editor_free_stack(char **stack, int *count)
{
    int i;

    for (i = 0; i < *count; i++)
        free(stack[i]);
    *count = 0;
}

static void editor_restore_snapshot(const char *snapshot)
{
    const char *p = snapshot;
    int line = 0;
    int len;

    while (*p && line < MAX_LINES) {
        const char *end = strchr(p, '\n');

        if (!end)
            end = p + strlen(p);
        len = (int)(end - p);
        if (len > MAX_LINE)
            len = MAX_LINE;
        memcpy(edit_lines[line], p, (size_t)len);
        edit_lines[line][len] = '\0';
        line++;
        p = *end ? end + 1 : end;
    }
    edit_line_count = line > 0 ? line : 1;
    if (line == 0)
        edit_lines[0][0] = '\0';
    if (edit_y >= edit_line_count)
        edit_y = edit_line_count - 1;
    if (edit_x > (int)strlen(edit_lines[edit_y]))
        edit_x = (int)strlen(edit_lines[edit_y]);
}

static void editor_push_undo(void)
{
    char *snapshot = editor_serialize();

    if (!snapshot)
        return;
    if (undo_count >= MAX_UNDO) {
        free(undo_stack[0]);
        memmove(undo_stack, undo_stack + 1, (size_t)(MAX_UNDO - 1) * sizeof(undo_stack[0]));
        undo_count = MAX_UNDO - 1;
    }
    undo_stack[undo_count++] = snapshot;
    editor_free_stack(redo_stack, &redo_count);
}

static void editor_undo(void)
{
    char *snapshot;
    char *current;

    if (undo_count <= 0)
        return;
    current = editor_serialize();
    if (current && redo_count < MAX_UNDO)
        redo_stack[redo_count++] = current;
    snapshot = undo_stack[--undo_count];
    editor_restore_snapshot(snapshot);
    free(snapshot);
    edit_dirty = 1;
}

static void editor_redo(void)
{
    char *snapshot;
    char *current;

    if (redo_count <= 0)
        return;
    current = editor_serialize();
    if (current && undo_count < MAX_UNDO)
        undo_stack[undo_count++] = current;
    snapshot = redo_stack[--redo_count];
    editor_restore_snapshot(snapshot);
    free(snapshot);
    edit_dirty = 1;
}

static void editor_insert_char(int ch)
{
    int len = (int)strlen(edit_lines[edit_y]);

    if (len >= MAX_LINE)
        return;
    editor_push_undo();
    if (edit_x < 0)
        edit_x = 0;
    if (edit_x > len)
        edit_x = len;
    memmove(edit_lines[edit_y] + edit_x + 1,
            edit_lines[edit_y] + edit_x,
            (size_t)(len - edit_x + 1));
    edit_lines[edit_y][edit_x++] = (char)ch;
    edit_dirty = 1;
}

static void editor_backspace(void)
{
    int len;

    if (edit_x > 0) {
        editor_push_undo();
        len = (int)strlen(edit_lines[edit_y]);
        memmove(edit_lines[edit_y] + edit_x - 1,
                edit_lines[edit_y] + edit_x,
                (size_t)(len - edit_x + 1));
        edit_x--;
        edit_dirty = 1;
    } else if (edit_y > 0) {
        int prev_len = (int)strlen(edit_lines[edit_y - 1]);
        int cur_len = (int)strlen(edit_lines[edit_y]);
        if (prev_len + cur_len <= MAX_LINE) {
            editor_push_undo();
            strcat(edit_lines[edit_y - 1], edit_lines[edit_y]);
            memmove(edit_lines + edit_y, edit_lines + edit_y + 1,
                    (size_t)(edit_line_count - edit_y - 1) * sizeof(edit_lines[0]));
            edit_line_count--;
            edit_y--;
            edit_x = prev_len;
            edit_dirty = 1;
        }
    }
}

static void current_line_list_prefix(char *out, size_t out_size)
{
    const char *line = edit_lines[edit_y];
    int i = 0;

    out[0] = '\0';
    while ((line[i] == ' ' || line[i] == '\t') && i + 1 < (int)out_size) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
    if ((line[i] == '-' || line[i] == '*' || line[i] == '+')
               && line[i + 1] == ' ' && line[i + 2] == '['
               && (line[i + 3] == ' ' || line[i + 3] == 'x' || line[i + 3] == 'X')
               && line[i + 4] == ']' && line[i + 5] == ' ') {
        append_string(out, out_size, "- [ ] ");
    } else if ((line[i] == '-' || line[i] == '*' || line[i] == '+') && line[i + 1] == ' ') {
        append_string(out, out_size, "- ");
    } else if (isdigit((unsigned char)line[i])) {
        int start = i;
        while (isdigit((unsigned char)line[i]))
            i++;
        if (line[i] == '.' && line[i + 1] == ' ') {
            append_nstring(out, out_size, line + start, (size_t)(i - start + 2));
        }
    }
}

static void editor_newline(void)
{
    char tail[MAX_LINE + 1];
    char prefix[MAX_LINE + 1];

    if (edit_line_count >= MAX_LINES)
        return;
    editor_push_undo();
    if (edit_x < 0)
        edit_x = 0;
    if (edit_x > (int)strlen(edit_lines[edit_y]))
        edit_x = (int)strlen(edit_lines[edit_y]);
    copy_string(tail, sizeof(tail), edit_lines[edit_y] + edit_x);
    current_line_list_prefix(prefix, sizeof(prefix));
    memmove(edit_lines + edit_y + 2, edit_lines + edit_y + 1,
            (size_t)(edit_line_count - edit_y - 1) * sizeof(edit_lines[0]));
    copy_string(edit_lines[edit_y + 1], sizeof(edit_lines[edit_y + 1]), prefix);
    append_string(edit_lines[edit_y + 1], sizeof(edit_lines[edit_y + 1]), tail);
    edit_lines[edit_y][edit_x] = '\0';
    edit_line_count++;
    edit_y++;
    edit_x = (int)strlen(prefix);
    edit_dirty = 1;
}

static void editor_toggle_checkbox(void)
{
    char *line = edit_lines[edit_y];
    char *mark = strstr(line, "[ ]");

    if (!mark)
        mark = strstr(line, "[x]");
    if (!mark)
        mark = strstr(line, "[X]");
    if (!mark)
        return;
    editor_push_undo();
    if (mark[1] == ' ')
        mark[1] = 'x';
    else
        mark[1] = ' ';
    edit_dirty = 1;
}

static void editor_autocomplete_link(void)
{
    char *line = edit_lines[edit_y];
    char replacement[MAX_TITLE];
    char prefix[MAX_TITLE];
    char *start = NULL;
    char *p = line;
    char *end;
    int prefix_len = 0;
    int match = -1;
    int i;
    int cursor = edit_x;

    while ((p = strstr(p, "[[")) != NULL) {
        end = strstr(p + 2, "]]");
        if (p - line <= cursor && (!end || end - line >= cursor))
            start = p + 2;
        p += 2;
    }
    if (!start) {
        editor_insert_char('\t');
        return;
    }
    end = line + cursor;
    while (start + prefix_len < end
           && start[prefix_len] != '|'
           && start[prefix_len] != '#'
           && prefix_len + 1 < (int)sizeof(prefix))
        prefix[prefix_len++] = start[prefix_len];
    prefix[prefix_len] = '\0';

    if (prefix[0] == '\0') {
        set_status("Type part of a note title first");
        return;
    }

    if (strcmp(autocomplete_prefix, prefix) != 0)
        autocomplete_last_match = -1;

    for (i = 0; i < note_count; i++) {
        int candidate = (autocomplete_last_match + 1 + i) % note_count;

        if (case_starts_with(notes[candidate].title, prefix)
            || case_starts_with(notes[candidate].display_title, prefix)) {
            match = candidate;
            break;
        }
    }
    if (match < 0) {
        set_status("No note matches link prefix");
        autocomplete_last_match = -1;
        autocomplete_prefix[0] = '\0';
        return;
    }

    copy_string(autocomplete_prefix, sizeof(autocomplete_prefix), prefix);
    autocomplete_last_match = match;
    copy_string(replacement, sizeof(replacement), notes[match].title);
    if ((int)(strlen(line) - prefix_len + strlen(replacement)) >= MAX_LINE) {
        set_status("Completed link would exceed line limit");
        return;
    }
    editor_push_undo();
    memmove(start + strlen(replacement), start + prefix_len,
            strlen(start + prefix_len) + 1);
    memcpy(start, replacement, strlen(replacement));
    edit_x += (int)strlen(replacement) - prefix_len;
    edit_dirty = 1;
}

static int editor_find_next(const char *query)
{
    int start = edit_y + 1;
    int i;

    for (i = start; i < edit_line_count; i++) {
        if (case_contains(edit_lines[i], query)) {
            edit_y = i;
            if (edit_x > (int)strlen(edit_lines[edit_y]))
                edit_x = (int)strlen(edit_lines[edit_y]);
            edit_find_line = i;
            return 1;
        }
    }
    for (i = 0; i <= edit_y; i++) {
        if (case_contains(edit_lines[i], query)) {
            edit_y = i;
            if (edit_x > (int)strlen(edit_lines[edit_y]))
                edit_x = (int)strlen(edit_lines[edit_y]);
            edit_find_line = i;
            return 1;
        }
    }
    return 0;
}

static void run_editor(void)
{
    int ch, len;

    if (current_note < 0)
        return;
    load_editor();
    edit_dirty = 0;
    edit_quit_confirm = 0;
    edit_find_line = -1;
    edit_find_query[0] = '\0';
    autocomplete_prefix[0] = '\0';
    autocomplete_last_match = -1;
    editor_free_stack(undo_stack, &undo_count);
    editor_free_stack(redo_stack, &redo_count);
    curs_set(1);
    for (;;) {
        draw_editor();
        ch = getch();
        len = (int)strlen(edit_lines[edit_y]);
        if (ch == 27) {
            if (edit_dirty && !edit_quit_confirm) {
                set_status("Unsaved changes: Esc again to discard");
                edit_quit_confirm = 1;
                continue;
            }
            set_status(edit_dirty ? "Edit discarded" : "Edit cancelled");
            break;
        }
        edit_quit_confirm = 0;
        if (ch == CTRL_KEY('x')) {
            save_editor();
            break;
        }
        if (ch == CTRL_KEY('z')) {
            editor_undo();
            continue;
        }
        if (ch == CTRL_KEY('y')) {
            editor_redo();
            continue;
        }
        if (ch == CTRL_KEY('t')) {
            editor_toggle_checkbox();
            continue;
        }
        if (ch == CTRL_KEY('f')) {
            copy_string(edit_find_query, sizeof(edit_find_query), "");
            if (prompt_text("Find in note: ", edit_find_query, sizeof(edit_find_query))) {
                if (!editor_find_next(edit_find_query))
                    set_status("No match in note");
            }
            continue;
        }
        if (ch == 'n' && edit_find_query[0]) {
            if (!editor_find_next(edit_find_query))
                set_status("No further match");
            continue;
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
        } else if (ch == '\t') {
            editor_autocomplete_link();
        } else if (ch == '\n' || ch == '\r') {
            editor_newline();
        } else if (isprint(ch)) {
            editor_insert_char(ch);
        }
    }
    curs_set(0);
    editor_free_stack(undo_stack, &undo_count);
    editor_free_stack(redo_stack, &redo_count);
}

static void open_note_at_line(int idx, int source_line)
{
    if (idx < 0 || idx >= note_count)
        return;
    load_note_view(idx);
    jump_to_source_line(source_line);
}

static void open_note_recording_history(int idx, int source_line)
{
    if (current_note >= 0 && current_note != idx) {
        history_push_current(history_back, &history_back_count);
        clear_forward_history();
    }
    open_note_at_line(idx, source_line);
}

static void navigate_history(HistoryEntry *from, int *from_count,
                             HistoryEntry *to, int *to_count)
{
    int idx;

    if (*from_count <= 0)
        return;
    history_push_current(to, to_count);
    (*from_count)--;
    idx = find_note_by_target(from[*from_count].title);
    if (idx >= 0)
        open_note_at_line(idx, from[*from_count].line);
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
    i = find_note_by_target(links[idx].target);
    if (i >= 0) {
        open_note_recording_history(i, 0);
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
    if (create_note(links[idx].target)) {
        i = find_note_by_target(links[idx].target);
        if (i >= 0)
            open_note_recording_history(i, 0);
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

static void cycle_theme(void)
{
    int idx = theme_index_from_name(theme_name);
    char msg[MAX_STATUS];

    idx = (idx + 1) % THEME_COUNT;
    copy_string(theme_name, sizeof(theme_name), theme_names[idx]);
    init_theme();
    sprintf(msg, "Theme: %s", theme_name);
    set_status(msg);
    save_state();
}

static void save_current_search(void)
{
    char name[MAX_TITLE];
    int i;

    if (current_search_query[0] == '\0') {
        set_status("Run a search first");
        return;
    }
    copy_string(name, sizeof(name), current_search_query);
    if (!prompt_text("Save search as: ", name, sizeof(name)))
        return;
    for (i = 0; i < saved_search_count; i++) {
        if (strcmp(saved_searches[i].name, name) == 0) {
            copy_string(saved_searches[i].query, sizeof(saved_searches[i].query),
                        current_search_query);
            save_saved_searches();
            set_status("Saved search updated");
            return;
        }
    }
    if (saved_search_count >= MAX_SAVED_SEARCHES) {
        set_status("Saved search list full");
        return;
    }
    copy_string(saved_searches[saved_search_count].name,
                sizeof(saved_searches[saved_search_count].name), name);
    copy_string(saved_searches[saved_search_count].query,
                sizeof(saved_searches[saved_search_count].query), current_search_query);
    saved_search_count++;
    save_saved_searches();
    set_status("Saved search added");
}

static void run_command_palette_action(int action)
{
    char input[MAX_TITLE];
    char template_name[MAX_TITLE];

    input[0] = '\0';
    template_name[0] = '\0';
    current_panel = PANEL_NOTE;
    if (action == 1) {
        if (prompt_text("New note: ", input, sizeof(input)))
            create_note(input);
    } else if (action == 2) {
        if (prompt_text("Template file: ", template_name, sizeof(template_name))
            && prompt_text("New note: ", input, sizeof(input)))
            create_note_with_template(input, template_name);
    } else if (action == 3) {
        open_daily_note();
    } else if (action == 4) {
        run_editor();
    } else if (action == 5) {
        if (prompt_text("Find in notes: ", input, sizeof(input)))
            run_full_text_search(input);
    } else if (action == 6) {
        build_backlinks();
    } else if (action == 7) {
        build_mentions();
    } else if (action == 8) {
        open_outline();
    } else if (action == 9) {
        open_tags();
    } else if (action == 10) {
        open_saved_searches();
    } else if (action == 11) {
        build_note_info();
    } else if (action == 12) {
        read_mode = !read_mode;
        save_state();
    } else if (action == 13) {
        show_sidebar = !show_sidebar;
        save_state();
    } else if (action == 14) {
        sort_mode = (sort_mode + 1) % 3;
        load_notes();
        save_state();
    } else if (action == 15) {
        cycle_theme();
    } else if (action == 16) {
        rename_current_note();
    } else if (action == 17) {
        delete_current_note();
    }
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
        else if (current_panel == PANEL_TAGS)
            panel_move(-1, tag_count);
        else if (current_panel == PANEL_MENTIONS)
            panel_move(-1, mention_count);
        else if (current_panel == PANEL_INFO)
            panel_move(-1, info_line_count);
        else if (current_panel == PANEL_SAVED)
            panel_move(-1, saved_search_count);
        else if (current_panel == PANEL_COMMANDS)
            panel_move(-1, 17);
        return;
    }
    if (ch == KEY_DOWN || ch == 'j') {
        if (current_panel == PANEL_BACKLINKS)
            panel_move(1, backlink_count);
        else if (current_panel == PANEL_SEARCH)
            panel_move(1, search_result_count);
        else if (current_panel == PANEL_OUTLINE)
            panel_move(1, heading_count);
        else if (current_panel == PANEL_TAGS)
            panel_move(1, tag_count);
        else if (current_panel == PANEL_MENTIONS)
            panel_move(1, mention_count);
        else if (current_panel == PANEL_INFO)
            panel_move(1, info_line_count);
        else if (current_panel == PANEL_SAVED)
            panel_move(1, saved_search_count);
        else if (current_panel == PANEL_COMMANDS)
            panel_move(1, 17);
        return;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (current_panel == PANEL_BACKLINKS && panel_selected < backlink_count) {
            idx = backlink_indices[panel_selected];
            open_note_recording_history(idx, 0);
        } else if (current_panel == PANEL_SEARCH && panel_selected < search_result_count) {
            open_note_recording_history(search_results[panel_selected].note_index,
                                        search_results[panel_selected].line);
        } else if (current_panel == PANEL_OUTLINE && panel_selected < heading_count) {
            selected_heading = panel_selected;
            current_panel = PANEL_NOTE;
            jump_to_source_line(headings[panel_selected].line);
        } else if (current_panel == PANEL_TAGS && panel_selected < tag_count) {
            selected_tag = panel_selected;
            if (strcmp(active_tag, tags[panel_selected].name) == 0)
                active_tag[0] = '\0';
            else
                copy_string(active_tag, sizeof(active_tag), tags[panel_selected].name);
            build_sidebar();
            current_panel = PANEL_NOTE;
            selected_note = 0;
            top_note = 0;
            save_state();
        } else if (current_panel == PANEL_MENTIONS && panel_selected < mention_count) {
            idx = mention_indices[panel_selected];
            open_note_recording_history(idx, 0);
        } else if (current_panel == PANEL_SAVED && panel_selected < saved_search_count) {
            selected_saved_search = panel_selected;
            run_full_text_search(saved_searches[panel_selected].query);
        } else if (current_panel == PANEL_COMMANDS) {
            selected_command = panel_selected;
            run_command_palette_action(panel_selected + 1);
        }
    }
}

static void handle_main_key(int ch)
{
    int visible_total = sidebar_item_count;
    int idx;
    char input[MAX_TITLE];
    SidebarItem *item;

    if (current_panel != PANEL_NOTE) {
        handle_panel_key(ch);
        return;
    }

    status_msg[0] = '\0';
    build_sidebar();
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
        if (show_sidebar && selected_note >= 0 && selected_note < sidebar_item_count) {
            item = &sidebar_items[selected_note];
            if (item->kind == SIDEBAR_KIND_DIR && dirs[item->dir_index].expanded) {
                dirs[item->dir_index].expanded = 0;
                build_sidebar();
            } else if (note_scroll > 0) {
                note_scroll--;
            }
        } else if (note_scroll > 0) {
            note_scroll--;
        }
    } else if (ch == KEY_RIGHT || ch == 'l') {
        if (show_sidebar && selected_note >= 0 && selected_note < sidebar_item_count) {
            item = &sidebar_items[selected_note];
            if (item->kind == SIDEBAR_KIND_DIR && !dirs[item->dir_index].expanded) {
                dirs[item->dir_index].expanded = 1;
                build_sidebar();
            } else {
                note_scroll++;
            }
        } else {
            note_scroll++;
        }
    } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (selected_note >= 0 && selected_note < sidebar_item_count) {
            item = &sidebar_items[selected_note];
            if (item->kind == SIDEBAR_KIND_DIR) {
                dirs[item->dir_index].expanded = !dirs[item->dir_index].expanded;
                build_sidebar();
            } else if (item->kind == SIDEBAR_KIND_NOTE) {
                idx = item->note_index;
                if (idx == current_note && link_count > 0) {
                    follow_link(selected_link);
                } else if (idx >= 0) {
                    open_note_recording_history(idx, 0);
                }
            }
        }
    } else if (ch == '\t') {
        if (link_count > 0)
            selected_link = (selected_link + 1) % link_count;
    } else if (ch == KEY_BTAB) {
        if (link_count > 0)
            selected_link = (selected_link + link_count - 1) % link_count;
    } else if (ch == key_new_note) {
        input[0] = '\0';
        if (prompt_text("New note: ", input, sizeof(input)))
            create_note(input);
    } else if (ch == key_template_note) {
        char template_name[MAX_TITLE];

        template_name[0] = '\0';
        input[0] = '\0';
        if (prompt_text("Template file: ", template_name, sizeof(template_name))
            && prompt_text("New note: ", input, sizeof(input)))
            create_note_with_template(input, template_name);
    } else if (ch == key_daily_note) {
        open_daily_note();
    } else if (ch == key_edit_note) {
        run_editor();
    } else if (ch == key_delete_note) {
        delete_current_note();
    } else if (ch == key_rename_note) {
        rename_current_note();
    } else if (ch == key_filter_titles) {
        if (prompt_text("Filter titles: ", note_filter, sizeof(note_filter))) {
            selected_note = 0;
            top_note = 0;
            build_sidebar();
        }
    } else if (ch == key_full_text_search) {
        input[0] = '\0';
        if (prompt_text("Find in notes: ", input, sizeof(input)))
            run_full_text_search(input);
    } else if (ch == 27) {
        note_filter[0] = '\0';
    } else if (ch == key_backlinks) {
        build_backlinks();
    } else if (ch == key_mentions) {
        build_mentions();
    } else if (ch == key_outline) {
        open_outline();
    } else if (ch == key_tags) {
        open_tags();
    } else if (ch == key_saved_searches) {
        open_saved_searches();
    } else if (ch == key_command_palette) {
        current_panel = PANEL_COMMANDS;
        panel_selected = selected_command;
        panel_scroll = 0;
    } else if (ch == key_note_info) {
        build_note_info();
    } else if (ch == key_toggle_mode) {
        read_mode = !read_mode;
        save_state();
    } else if (ch == key_toggle_sidebar) {
        show_sidebar = !show_sidebar;
        save_state();
    } else if (ch == key_cycle_sort) {
        sort_mode = (sort_mode + 1) % 3;
        load_notes();
        save_state();
    } else if (ch == key_cycle_theme) {
        cycle_theme();
    } else if (ch == key_history_back) {
        navigate_history(history_back, &history_back_count,
                         history_forward, &history_forward_count);
    } else if (ch == key_history_forward) {
        navigate_history(history_forward, &history_forward_count,
                         history_back, &history_back_count);
    } else if (ch == key_save_search) {
        save_current_search();
    }
}

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void init_theme(void)
{
    short fg = COLOR_WHITE;
    short bg = COLOR_BLUE;
    int idx;

    if (!has_colors())
        return;
    idx = theme_index_from_name(theme_name);
    copy_string(theme_name, sizeof(theme_name), theme_names[idx]);
    if (idx == 1) {
        fg = COLOR_YELLOW;
        bg = COLOR_BLACK;
    } else if (idx == 2) {
        fg = COLOR_GREEN;
        bg = COLOR_BLACK;
    } else if (idx == 3) {
        fg = COLOR_CYAN;
        bg = COLOR_BLACK;
    }
    start_color();
    init_pair(1, fg, bg);
    bkgd(COLOR_PAIR(1));
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
    load_config();
    load_saved_searches();
    load_notes();
    load_state();
    load_notes();

    if (case_equals(startup_mode, "daily") || case_equals(startup_mode, "none")) {
        startup_applied = 1;
    }

    if (!startup_applied && last_open_title[0]) {
        for (i = 0; i < note_count; i++) {
            if (strcmp(notes[i].title, last_open_title) == 0) {
                load_note_view(i);
                startup_applied = 1;
                break;
            }
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    init_theme();
    curs_set(0);

    if (case_equals(startup_mode, "daily"))
        open_daily_note();

    set_status("p palette  f search  b backlinks  u mentions  v saved  i info  Enter open/follow  e edit  q quit");
    while (running) {
        draw_main();
        ch = getch();
        handle_main_key(ch);
    }

    save_state();
    endwin();
    free_view();
    free_note_indices();
    return 0;
}
