# `memex` DOS Port Design

This document describes the design of the DOS port as it was implemented on
the `dos-port` branch. The target is a 32-bit protected-mode DOS executable
using DJGPP and PDCurses, running on a 386SX/8 MB class machine.

## Goals

- Keep the Linux build working throughout the port.
- Avoid scattering `#ifdef` guards across `memex.c`.
- Express DOS-specific behavior through a small platform layer and a separate
  build target.
- Require long filename support for the first working port (`MEMEX_DOS_PROFILE`).
- Support bare MS-DOS 6.22 FAT16 without any LFN driver via `MEMEX_DOS_FAT`.
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
| `dos622-test.conf` | DOSBox-X config for `lfn=false` runtime testing |
| `runtest.bat` | DOS batch file to drive smoke/persistence tests |

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

### LFN build (`MEMEX_DOS_PROFILE` without `MEMEX_DOS_FAT`)

Dot-prefixed support files are kept as-is; the port requires long filename
support in this mode:

- `.memex-state`
- `.memexrc`
- `.memex-searches`
- `.memex-daily-format`
- `.trash/`
- `.templates/`

Note title validation in `memex.c` rejects DOS reserved device names
(`CON`, `AUX`, `COM1`–`COM9`, `LPT1`–`LPT9`, `NUL`, `PRN`) and strips
trailing dots and spaces to prevent filesystem errors on DOS.

### 8.3 FAT build (`MEMEX_DOS_FAT`)

`MEMEX_DOS_FAT` implies `MEMEX_DOS_PROFILE` and additionally replaces every
special filename with an 8.3-legal equivalent defined in `memex_config.h`:

| Purpose | LFN name | 8.3 name |
|---|---|---|
| State | `.memex-state` | `MXSTATE.DAT` |
| Config | `.memexrc` | `MEMEXRC.CFG` |
| Saved searches | `.memex-searches` | `MXSRCH.DAT` |
| Daily format | `.memex-daily-format` | `MXDAYFMT.DAT` |
| Trash directory | `.trash/` | `TRASH/` |
| Template directory | `.templates/` | `TEMPLATE/` |
| Rewrite scratch | `.memex-rewrite.tmp` | `MXRWRT.TMP` |

Note filenames are sanitized and truncated to 8 characters before the `.MD`
extension is appended. If two titles produce the same 8-char stem, `~1`
through `~9` suffixes are tried (e.g., `MEETIN~1.MD`, `MEETIN~2.MD`) and the
caller surfaces an error if all suffixes are exhausted.

#### Display title roundtrip

Under 8.3, a note titled "Meeting Notes" is stored as `MEETINGN.MD`. On
reload, `note->title` is derived from the 8.3 filename stem (`"MEETINGN"`),
but `note->display_title` is set from the `# Heading` written in the file
by `write_note_template`. The heading is written using the original user input,
not the 8.3 stem, so the full title survives the filename truncation.

`find_note_by_target` checks both `title` and `display_title`, so
`[[Meeting Notes]]` links resolve correctly even when the file on disk is
`MEETINGN.MD`. `sanitize_rel_title` applies `sanitize_title` per `/`-separated
path segment independently, so nested note directories each get their own 8-char
truncation.

The UX consequence is intentional: note filenames are opaque 8.3 identifiers
on disk, but the application always shows and searches the full title from
`# headings` or YAML `title:` frontmatter.

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
make -f Makefile.dj          (LFN build: memex.exe with MEMEX_DOS_PROFILE)
make -f Makefile.dj fat      (FAT build: memex.exe with MEMEX_DOS_FAT)
make -f Makefile.dj check-syntax
make -f Makefile.dj check-fat
make -f Makefile.dj check-triage
```

`check-syntax` and `check-fat` compile without linking — useful on hosts
that do not have PDCurses installed. `check-fat` includes `-DMEMEX_DOS_FAT`
and is the primary syntax-validation target for the DOS 6.22 build.
`check-triage` compiles the reduced-feature build.

For a Linux-hosted DJGPP cross-compiler:

```sh
make -f Makefile.dj CC=i686-pc-msdosdjgpp-gcc
make -f Makefile.dj fat CC=i686-pc-msdosdjgpp-gcc \
    LIBS="/path/to/PDCursesMod/dos/pdcurses.a" \
    CFLAGS="-O2 -Wall -march=i386 -DMEMEX_DOS_PROFILE -DMEMEX_DISABLE_MOUSE -I/path/to/PDCursesMod"
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

### LFN build (`MEMEX_DOS_PROFILE`)

Phases 1–12 are complete. The source passes all host checks and a linked
`memex.exe` has been confirmed under DOSBox-X with `lfn=true`:

- `make` / `make smoke` / `make persistence` / `make performance` / `make triage`
- `make -f Makefile.dj check-syntax` / `make -f Makefile.dj check-triage`
- `memex.exe --smoke-test` and `--persistence-test` under DOSBox-X
- Interactive TUI launches, navigates, creates, edits, saves, and renames notes

See `DOS_BUILD.md` for the confirmed build environment (GCC 14.2.0, DJGPP 2.05,
PDCursesMod DOS backend).

### FAT build (`MEMEX_DOS_FAT`)

The source changes and `fat` Makefile target are complete. All host checks and
DOSBox-X runtime verification pass:

- `make smoke` / `make persistence` (Linux, no flags)
- `make smoke` / `make persistence` compiled with `-DMEMEX_DOS_FAT -DMEMEX_DOS_PROFILE`
- `make -f Makefile.dj check-fat` (syntax check with FAT flags)
- `memex.exe --smoke-test S:\` → `smoke: PASS` (DOSBox-X 2024.03.01, `lfn=false`)
- `memex.exe --persistence-test P:\` → `persistence: PASS` (same environment)

See `DOS_BUILD.md` for the confirmed build environment (GCC 12.2.0,
`i586-pc-msdosdjgpp-gcc`, PDCursesMod v4.5.4, `andrewwutw/build-djgpp` v3.4)
and for three runtime bugs found and fixed during FAT testing.

The following remain pending:

- Confirm interactive TUI with 8.3 note filenames (requires console session).
- Confirm CWSDPMI loads on bare MS-DOS 6.22 (no built-in DPMI).
- Performance test (`--performance-test`) on real DOS hardware.
