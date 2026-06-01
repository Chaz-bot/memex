# Porting `memex` to DOS

This document describes what it would take to port the current Linux-targeted `memex` codebase to DOS on a Pocket 386 class machine with a 386SX CPU and 8 MB of RAM.

Short answer: a DOS build is possible in principle, but not from the current source tree without a real compatibility layer and a memory-reduction pass.

## Current State

The code is currently written for old Linux, not DOS.

Relevant source assumptions:

- `ncurses` UI in [Makefile](./Makefile) and [memex.c](./memex.c)
- POSIX headers in [memex.c](./memex.c): `dirent.h`, `unistd.h`, `signal.h`, `sys/stat.h`
- POSIX-style filesystem and process behavior throughout [memex.c](./memex.c)
- Large static arrays sized for convenience, not for a tight DOS memory model

## Main Blockers

### 1. Terminal UI library

The program uses `curses.h` and common curses behaviors such as:

- `initscr`
- `cbreak`
- `noecho`
- `keypad`
- `getch`
- `start_color`
- `init_pair`
- `A_REVERSE`
- `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_NPAGE`, `KEY_PPAGE`, `KEY_ENTER`

That is workable on DOS only if you replace `ncurses` with a DOS-compatible curses implementation, most likely `PDCurses`.

### 2. POSIX directory traversal

The note scanner depends on:

- `opendir`
- `readdir`
- `closedir`
- `struct dirent`

Those are Linux/POSIX interfaces. DOS toolchains may provide substitutes, but you should assume you need a wrapper layer.

Primary scan sites in [memex.c](./memex.c):

- note loading logic around `opendir` / `readdir`
- rename/link rewrite directory walks

### 3. POSIX filesystem behavior

The code expects POSIX-like behavior for:

- `mkdir(path, 0777)`
- `stat`
- `rename`
- `getcwd`
- `/` path separators
- dot-prefixed directories and files like `.trash`, `.templates`, `.memexrc`

DOS can handle some of this under DJGPP, but not all semantics are safe to assume.

### 4. Memory footprint

The source keeps a lot of data in global fixed-size arrays. That is convenient on Linux, but on DOS with 8 MB RAM it is the main runtime risk.

Important compile-time limits near the top of [memex.c](./memex.c):

- `MAX_NOTES 512`
- `MAX_LINES 2048`
- `MAX_RENDERED 8192`
- `MAX_RESULTS 256`
- `MAX_BACKLINKS 256`
- `MAX_MENTIONS 256`
- `MAX_SIDEBAR_ITEMS 1024`
- `PATH_MAX 1024`

The largest pressure points are:

- `static Note notes[MAX_NOTES]`
- `static DisplayLine rendered_lines[MAX_RENDERED]`
- `static NoteIndex note_index[MAX_NOTES]`
- `static DirectoryInfo dirs[MAX_DIRS]`
- `static SidebarItem sidebar_items[MAX_SIDEBAR_ITEMS]`
- `static char edit_lines[MAX_LINES][MAX_LINE + 1]`

The 386SX CPU is not the real problem here. The memory budget is.

## Recommended Target

If you want real DOS support, target this combination first:

- Compiler: `DJGPP`
- TUI library: `PDCurses`
- Memory model: 32-bit protected mode DOS extender environment

Do not target 16-bit Turbo C or Open Watcom first. The program is already large enough that a 16-bit first port would force extra memory-model work too early.

## Porting Strategy

Do this in phases. Do not start by editing random call sites.

### Phase 1: Introduce a platform layer

Create a small compatibility boundary instead of scattering `#ifdef DOS` across the whole file.

Add a new abstraction header and implementation pair, for example:

- `platform.h`
- `platform_posix.c`
- `platform_dos.c`

Move these responsibilities behind wrappers:

- directory iteration
- path joining and separator normalization
- `mkdir`
- `rename`
- `stat`-based existence checks
- current directory lookup
- interrupt handling

Suggested wrapper surface:

```c
int platform_init(void);
void platform_shutdown(void);
int platform_getcwd(char *buf, size_t size);
int platform_mkdir(const char *path);
int platform_file_exists(const char *path);
int platform_is_dir(const char *path);
int platform_rename(const char *old_path, const char *new_path);
char platform_path_sep(void);
```

For directory scanning, define a tiny iterator API instead of exposing `DIR *`:

```c
typedef struct PlatformDir PlatformDir;

PlatformDir *platform_opendir(const char *path);
int platform_readdir(PlatformDir *dir, char *name, size_t size);
void platform_closedir(PlatformDir *dir);
```

### Phase 2: Isolate curses usage

The UI calls are all in one file today, but they are still direct `curses` calls. Keep the rendering model, but make the curses dependency swappable.

At minimum:

- keep `curses.h` includes behind a single compatibility include
- centralize color initialization
- centralize key normalization

You want one translation unit deciding whether `KEY_BACKSPACE`, `KEY_BTAB`, `KEY_ENTER`, and color support behave the same on Linux and DOS.

### Phase 3: Reduce static memory

Before attempting a DOS build, cut the compile-time limits aggressively.

Suggested first DOS profile:

- `MAX_NOTES`: `512 -> 128`
- `MAX_RENDERED`: `8192 -> 2048`
- `MAX_RESULTS`: `256 -> 96`
- `MAX_BACKLINKS`: `256 -> 96`
- `MAX_MENTIONS`: `256 -> 96`
- `MAX_DIRS`: `256 -> 96`
- `MAX_SIDEBAR_ITEMS`: `1024 -> 256`
- `MAX_LINES`: `2048 -> 1024`
- `MAX_HISTORY`: `128 -> 32`
- `MAX_SAVED_SEARCHES`: `32 -> 16`
- `PATH_MAX`: `1024 -> 260`

That should be controlled by a build profile, not hand-edited constants.

Suggested pattern near the top of [memex.c](./memex.c):

```c
#ifdef MEMEX_DOS_PROFILE
#define PATH_MAX 260
#define MAX_NOTES 128
#define MAX_LINES 1024
#define MAX_RENDERED 2048
...
#else
#define PATH_MAX 1024
#define MAX_NOTES 512
#define MAX_LINES 2048
#define MAX_RENDERED 8192
...
#endif
```

Better still, move the limits into a dedicated config header.

### Phase 4: Fix path and filename assumptions

DOS-specific risks:

- backslash vs slash
- possible 8.3 filename environments
- reserved device names
- hidden-file conventions are not Unix-like

The current code assumes modern directory and filename behavior for:

- note files
- `.trash`
- `.templates`
- `.memexrc`
- `.memex-state`
- `.memex-searches`
- `.memex-daily-format`

Decide early whether the DOS target requires long filenames.

Recommended decision:

- require long filename support
- keep markdown files as `.md`
- keep subdirectories enabled

If long filenames are not guaranteed on the target DOS setup, this project becomes materially less practical.

### Phase 5: Build-system split

The current [Makefile](./Makefile) assumes `gcc` and `-lncurses`.

Add a separate DOS-oriented build file rather than overloading the Linux one immediately.

Example direction:

- `Makefile` for Linux
- `Makefile.dj` or `build-dos.bat` for DJGPP

The DOS build needs to:

- set a DOS profile define such as `-DMEMEX_DOS_PROFILE`
- link against `PDCurses`
- use the DOS platform implementation instead of the POSIX one

### Phase 6: Trim features only if necessary

If the first DOS build still feels memory-tight or too slow, cut features in this order:

1. saved searches
2. unlinked mentions
3. backlinks index breadth
4. full-text search result count
5. rendered line cache size
6. folder sidebar depth/detail

Do not remove the basic note browser/editor first. Preserve the core loop.

## Specific Code Areas To Change

These are the highest-value places to touch first in [memex.c](./memex.c):

- includes at the top of the file
- path handling helpers
- recursive note loading
- note directory creation
- trash and rename operations
- startup `getcwd` path initialization
- signal handling setup
- curses init and key handling

Concrete hotspots include the call sites around:

- `opendir` / `readdir`
- `mkdir`
- `rename`
- `stat`
- `getcwd`
- `signal(SIGINT, on_signal)`
- `initscr`, `start_color`, `keypad`, `getch`

## Suggested Implementation Order

1. Add a platform abstraction without changing behavior.
2. Move all filesystem and directory traversal through the abstraction.
3. Add a DOS memory profile with lower limits.
4. Add a curses compatibility include and key normalization layer.
5. Create a DJGPP + PDCurses build target.
6. Build on Linux again to confirm the refactor did not break the current target.
7. Attempt the first DOS build.
8. Fix compile errors and missing API assumptions.
9. Test with a very small note vault.
10. Measure memory pressure and reduce limits further if needed.

## Practical Risks

### High risk

- static memory still too large for comfortable 8 MB operation
- DOS filesystem edge cases around long filenames and nested paths
- curses key behavior differences under PDCurses

### Medium risk

- rename/trash semantics differing from Linux
- color support inconsistencies
- performance of full-text indexing on slower storage

### Lower risk

- CPU speed for normal note browsing and editing

The 386SX will be slow, but this code is simple enough that careful feature limits matter more than raw CPU.

## What I Would Do First

If continuing this port, the first concrete implementation step should be:

1. split platform-sensitive code out of [memex.c](./memex.c)
2. add a DOS memory profile header
3. keep Linux behavior unchanged while refactoring

That gives you a clean base for an actual DOS build instead of a growing pile of conditional compilation inside one large source file.

## Likely Outcome

Expected outcomes by environment:

- Old Linux on Pocket 386: realistic target for the current codebase
- DOS with DJGPP and PDCurses: possible with moderate refactoring and memory reduction
- Plain 16-bit DOS compiler target: not recommended for the first port

If you want the DOS version to be pleasant rather than merely booting, plan on reducing default limits and possibly making a few advanced features optional at compile time.
