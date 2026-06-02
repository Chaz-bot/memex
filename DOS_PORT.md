# `memex` DOS Port Design

This document describes the design of the DOS port as it was implemented on
the `dos-port` branch. The target is a 32-bit protected-mode DOS executable
using DJGPP and PDCurses, running on a 386SX/8 MB class machine.

## Goals

- Keep the Linux build working throughout the port.
- Avoid scattering `#ifdef` guards across `memex.c`.
- Express DOS-specific behavior through a small platform layer and a separate
  build target.
- Require long filename support for the first working port.
- Target 32-bit protected-mode DOS; defer 16-bit real-mode.

## Source Files

| File | Role |
|------|------|
| `memex.c` | Application code, unchanged for DOS compatibility |
| `memex_config.h` | Compile-time limits; selects DOS or Linux profile |
| `platform.h` | Platform API declaration |
| `platform_posix.c` | POSIX implementation (Linux build) |
| `platform_dos.c` | DOS implementation (DJGPP build) |
| `ui_curses.h` | Curses compatibility and key normalization |
| `Makefile` | Linux build |
| `Makefile.dj` | DJGPP/DOS build |
| `build-dos.bat` | DOS batch build entry point |

## Platform Layer

`platform.h` declares a small filesystem and directory API. `memex.c` calls
only these wrappers; it has no direct `opendir`, `readdir`, `getcwd`, `mkdir`,
`stat`, or `rename` calls.

```c
int platform_init(void);
void platform_shutdown(void);
int platform_getcwd(char *buf, size_t size);
int platform_mkdir(const char *path);
int platform_file_exists(const char *path);
int platform_is_dir(const char *path);
int platform_stat(const char *path, PlatformStat *st);
int platform_rename(const char *old_path, const char *new_path);
char platform_path_sep(void);

PlatformDir *platform_opendir(const char *path);
int platform_readdir(PlatformDir *dir, char *name, size_t size);
void platform_closedir(PlatformDir *dir);
```

`PlatformStat` carries `exists`, `is_dir`, `mtime`, and `ctime` as plain
`long` fields, avoiding `struct stat` from the POSIX headers.

`platform_posix.c` implements these with standard POSIX calls. It is linked
into the Linux build via `Makefile`.

`platform_dos.c` provides the same surface for DJGPP. It uses `<dirent.h>`,
`<sys/stat.h>`, and `<unistd.h>` as supplied by DJGPP, which covers the same
call sites as the POSIX implementation.

Paths inside `memex.c` use `/` as the internal separator. The platform layer
accepts both `/` and `\`; `platform_path_sep()` returns `'\\'` on both
implementations to match the DOS convention when building filesystem paths.

## Memory Profile

`memex_config.h` defines two compile-time profiles selected by
`-DMEMEX_DOS_PROFILE`.

### DOS profile limits

| Constant | Linux | DOS |
|----------|-------|-----|
| `MEMEX_PATH_MAX` | 1024 | 260 |
| `MAX_NOTES` | 512 | 128 |
| `MAX_LINES` | 2048 | 1024 |
| `MAX_RENDERED` | 8192 | 2048 |
| `MAX_RESULTS` | 256 | 96 |
| `MAX_BACKLINKS` | 256 | 96 |
| `MAX_MENTIONS` | 256 | 96 |
| `MAX_DIRS` | 256 | 96 |
| `MAX_SIDEBAR_ITEMS` | 1024 | 256 |
| `MAX_HISTORY` | 128 | 32 |
| `MAX_SAVED_SEARCHES` | 32 | 16 |

The Linux normal profile BSS is approximately 13.4 MB. The DOS profile BSS
is approximately 1.67 MB. With DPMI overhead, stack, and heap a machine with
4 MB of extended memory is the practical minimum; the design target is 8 MB.

### Optional feature switches

Features can be disabled individually at compile time if the target machine
is memory-constrained or PDCurses compatibility is incomplete:

| Define | Effect |
|--------|--------|
| `-DMEMEX_DISABLE_SAVED_SEARCHES` | Removes saved search persistence |
| `-DMEMEX_DISABLE_MENTIONS` | Removes unlinked mention indexing |
| `-DMEMEX_DISABLE_TRANSCLUSION` | Removes `![[Note]]` transclusion |
| `-DMEMEX_DISABLE_MOUSE` | Forces keyboard-only input |

All features are enabled by default including in `MEMEX_DOS_PROFILE`. The
host DOS-profile measurements do not justify trimming features before real
DOS runtime testing.

## Curses Compatibility

`ui_curses.h` is the only direct curses include in the codebase. It handles:

- Including `<curses.h>` (resolves to PDCurses on DJGPP, ncurses on Linux).
- Detecting mouse support via `KEY_MOUSE` and `MEMEX_DISABLE_MOUSE`.
- Detecting `KEY_BTAB` availability.
- Normalizing raw key values to a stable `MEMEX_KEY_*` enum so the
  application layer does not branch on curses-version differences.
- Centralizing color pair initialization in `ui_start_theme_color`.
- Providing `ui_init_keyboard`, `ui_read_key`, and per-key predicate helpers.

Keys normalized: Enter, Backspace (three variants), Tab, Shift-Tab, Escape,
arrow keys, Page Up/Down, Home, End, and mouse events.

## Path And Filename Decisions

Paths are normalized to use `/` internally. The platform layer accepts both
separators when building filesystem paths for DOS.

Dot-prefixed support files are kept as-is since the port requires long
filename support:

- `.memex-state`
- `.memexrc`
- `.memex-searches`
- `.memex-daily-format`
- `.trash/`
- `.templates/`

Note title validation in `memex.c` rejects DOS reserved device names
(`CON`, `AUX`, `COM1`–`COM9`, `LPT1`–`LPT9`, `NUL`, `PRN`) and strips
trailing dots and spaces to prevent filesystem errors on DOS.

## Build System

### Linux

```sh
make
make smoke
make persistence
make performance
make triage
```

`triage` builds with all four feature-disable switches and runs the core and
persistence smoke tests to confirm the reduced build still works.

### DOS / DJGPP

```bat
make -f Makefile.dj
make -f Makefile.dj check-syntax
make -f Makefile.dj check-triage
```

`check-syntax` compiles without linking — useful on hosts that do not have
PDCurses installed. `check-triage` compiles the reduced-feature build.

For a Linux-hosted DJGPP cross-compiler:

```sh
make -f Makefile.dj CC=i586-pc-msdosdjgpp-gcc
```

## Smoke Tests

Three noninteractive test modes are built into `memex.c` and available from
the Linux host:

| Mode | Flag | Make target |
|------|------|-------------|
| Core runtime | `--smoke-test <dir>` | `make smoke` |
| Persistence / config | `--persistence-test <dir>` | `make persistence` |
| Performance / memory | `--performance-test <dir>` | `make performance` |

All three pass on the Linux host with and without `-DMEMEX_DOS_PROFILE`. They
do not initialize curses and can run inside a DOS environment once `memex.exe`
links.

### Host DOS-profile performance measurements

```
case25:    notes=25   load=0.002 s   index_heap=40 KB
case100:   notes=100  load=0.022 s   index_heap=159 KB
case_max:  notes=128  load=0.036 s   index_heap=204 KB
large_note: lines=192  editor_load=0.000 s  static_edit_buffer=241 KB
```

These are Linux-host numbers with `-DMEMEX_DOS_PROFILE`, not real DOS runtime
measurements.

## Current Status

Phases 1–11 are complete on the Linux host. The source passes:

- `make` (Linux build, normal profile)
- `make smoke` / `make persistence` / `make performance` (Linux host)
- `make triage` (all feature-disable switches)
- `make -f Makefile.dj check-syntax` (DJGPP-style syntax check, no PDCurses)
- `make -f Makefile.dj check-triage` (triage syntax check)

A full DOS binary linked against PDCurses has not yet been produced. The
following remain pending until real DOS hardware or emulator testing:

- Link `memex.exe` against PDCurses in a DJGPP environment.
- Confirm the executable starts and exits cleanly under FreeDOS/DOSBox-X.
- Confirm arrow, page, home, and end keys under PDCurses.
- Confirm create, edit, save, rename, trash, and reopen notes.
- Confirm links, backlinks, tags, outline, and search at DOS-profile limits.
- Confirm state and config persistence.
- Confirm acceptable performance on the target hardware.
- Confirm stable memory use across repeated open/edit/search cycles.
- Record tested compiler, PDCurses, DPMI provider, and runtime versions.
