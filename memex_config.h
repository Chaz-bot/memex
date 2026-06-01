#ifndef MEMEX_CONFIG_H
#define MEMEX_CONFIG_H

#include <limits.h>

#ifdef MEMEX_DOS_PROFILE

#define MEMEX_PATH_MAX 260
#define MAX_NOTES 128
#define MAX_NAME 80
#define MAX_TITLE 96
#define MAX_FILTER 80
#define MAX_LINE 240
#define MAX_LINES 1024
#define MAX_NOTE_BYTES (48 * 1024)
#define MAX_LINKS 96
#define MAX_STATUS 160
#define MAX_HEADINGS 128
#define MAX_RESULTS 96
#define MAX_BACKLINKS 96
#define MAX_RENDERED 2048
#define MAX_TAGS_PER_NOTE 16
#define MAX_TAGS 256
#define MAX_ALIASES 16
#define MAX_DIRS 96
#define MAX_SIDEBAR_ITEMS 256
#define MAX_PATH_PART 96
#define MAX_HISTORY 32
#define MAX_RECENT 12
#define MAX_UNDO 32
#define MAX_SAVED_SEARCHES 16
#define MAX_MENTIONS 96
#define MAX_INFO_LINES 32
#define MAX_COMMANDS 20

#else

#ifdef PATH_MAX
#define MEMEX_PATH_MAX PATH_MAX
#else
#define MEMEX_PATH_MAX 1024
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

#endif

#define STATE_FILE ".memex-state"
#define CONFIG_FILE ".memexrc"
#define SAVED_SEARCH_FILE ".memex-searches"
#define TRASH_DIR ".trash"
#define TEMPLATE_DIR ".templates"
#define DEFAULT_TEMPLATE "default.md"
#define DAILY_TEMPLATE "daily.md"
#define DAILY_FORMAT_FILE ".memex-daily-format"

#endif
