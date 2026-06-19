# memex

`memex` is an Obsidian-lite terminal note browser/editor for old Linux systems.
It is written as one small C89-ish program using curses, intended to build on
Slackware 4.0 and run acceptably on a Pocket 386 class machine with a 386SX and
8 MB of RAM.

## Features

- Plain `.md` notes in nested folders.
- Vim-like movement keys plus arrow keys where curses supports them.
- Mouse support where curses/PDCurses exposes serial or PS/2 mouse events.
- Create, rename, trash, and edit notes.
- YAML frontmatter for `title`, `tags`, `aliases`, and `pinned`.
- Tag browser/filter, pinned notes, recent notes, and sortable folder tree sidebar.
- Default and named note templates plus daily-note creation/open.
- Config file support for startup mode, theme, note directories, and key overrides.
- Toggle between read mode with wrapped rendered markdown and write mode with raw markdown.
- Search note titles.
- Indexed full-text search across note contents with jumpable results and saved searches.
- Follow `[[wiki links]]`, including `[[Note|Alias]]` and `[[Note#Heading]]`.
- Show backlinks, unlinked mentions, note info, heading outlines, and tags on demand.
- Link autocomplete while editing inside `[[...]]`.
- Transclusion support for `![[Note]]` plus improved table, fenced-code, blockquote, and callout rendering.
- Command palette for common actions.
- Navigation history for opened notes.
- Persist last-opened note, sidebar visibility, and preferred read/write mode.
- Tiny line-based editor with undo/redo, note-local find, checklist toggles, and list continuation.

## Build

Slackware 4.0 should have GCC, make, and ncurses development files available.

```sh
make
```

If your system has old curses but not ncurses, try:

```sh
make LIBS=-lcurses
```

## DOS Build

The `dos-port` branch carries a DJGPP/PDCurses port targeting 32-bit
protected-mode DOS on a 386SX/8 MB class machine.

### Status

The LFN build (`MEMEX_DOS_PROFILE`) is complete and confirmed in DOSBox-X.
The FAT build (`MEMEX_DOS_FAT`) for bare MS-DOS 6.22 is source-complete;
linking and runtime testing on a DOS 6.22 environment are pending.

- Platform layer, DOS memory profile, and curses compatibility layer complete.
- Smoke, persistence, and performance tests pass on the Linux host.
- `memex.exe` confirmed under DOSBox-X (LFN mode): smoke/persistence tests
  pass, interactive TUI launches and works.
- `make -f Makefile.dj check-fat` passes (FAT build syntax check).

See [DOS_BUILD.md](./DOS_BUILD.md) for build notes and confirmed environment,
and [DOS622_TODO.md](./DOS622_TODO.md) for DOS 6.22 remaining work.

### DOS Prerequisites

- **Compiler**: DJGPP GCC (32-bit protected-mode DOS toolchain). For
  Linux-hosted cross-compilation, `i686-pc-msdosdjgpp-gcc` works (the AUR
  `djgpp-gcc` package installs under this name).
- **Curses library**: PDCurses built for the same DJGPP target, with headers
  and `libpdcurses.a` (or `pdcurses.a` for PDCursesMod) in the search paths.
- **DPMI provider**: required at runtime for 32-bit protected-mode execution.
  `CWSDPMI.EXE` (included in this repository) loads automatically from the
  same directory as `memex.exe` when no DPMI host is present. DOSBox-X and
  FreeDOS provide their own DPMI and do not need `CWSDPMI.EXE`.
- **Long filename support**: required for the standard LFN build. Notes,
  templates, and state files use names longer than 8.3 characters. Under
  FreeDOS, the `DOSLFN` or `JLFN` TSR provides it; under DOSBox-X set
  `lfn=true`. **Not required** with the `MEMEX_DOS_FAT` build — see below.
- **make**: either a DJGPP-environment `make` or a DOS `make` capable of
  reading `Makefile.dj`.

### DOS Build Commands

**LFN build** (long filename support required at runtime):

```bat
make -f Makefile.dj
```

**FAT build** (bare MS-DOS 6.22 FAT16, no LFN needed):

```bat
make -f Makefile.dj fat
```

Or using the batch file:

```bat
build-dos.bat
```

For a Linux-hosted DJGPP cross-compiler:

```sh
make -f Makefile.dj CC=i686-pc-msdosdjgpp-gcc
make -f Makefile.dj fat CC=i686-pc-msdosdjgpp-gcc \
    LIBS="/path/to/PDCursesMod/dos/pdcurses.a" \
    CFLAGS="-O2 -Wall -march=i386 -DMEMEX_DOS_PROFILE -DMEMEX_DISABLE_MOUSE -I/path/to/PDCursesMod"
```

To verify the source compiles without linking (no PDCurses needed):

```bat
make -f Makefile.dj check-syntax
make -f Makefile.dj check-fat
```

To build with all optional features disabled:

```bat
make -f Makefile.dj check-triage
```

### DOS Runtime Requirements

- MS-DOS 6.22+, FreeDOS, or a compatible emulator (DOSBox-X, PCem, real hardware).
- A DPMI provider: `CWSDPMI.EXE` (included) for bare MS-DOS, or provided
  automatically by DOSBox-X / FreeDOS.
- **LFN build** (`MEMEX_DOS_PROFILE`): long filename support required (see above).
- **FAT build** (`MEMEX_DOS_FAT`): no LFN needed; runs on bare MS-DOS 6.22 FAT16.
- A serial or PS/2 mouse driver if mouse input is wanted; keyboard-only
  works without a mouse driver.

### Filename Scheme

Two builds are available, selectable at compile time:

**LFN build** (`make -f Makefile.dj`): requires long filename support in the
DOS environment. Uses dot-prefixed support names:

| Purpose | Filename |
|---|---|
| State | `.memex-state` |
| Config | `.memexrc` |
| Saved searches | `.memex-searches` |
| Daily format | `.memex-daily-format` |
| Trash | `.trash/` |
| Templates | `.templates/` |

**FAT build** (`make -f Makefile.dj fat`): runs on bare MS-DOS 6.22 FAT16
with no LFN driver. Uses 8.3-safe names:

| Purpose | Filename |
|---|---|
| State | `MXSTATE.DAT` |
| Config | `MEMEXRC.CFG` |
| Saved searches | `MXSRCH.DAT` |
| Daily format | `MXDAYFMT.DAT` |
| Trash | `TRASH/` |
| Templates | `TEMPLATE/` |

Note filenames in the FAT build use a sanitized title stem truncated to
8 characters with a `.MD` extension (e.g., a note titled "Meeting Notes"
is stored as `MEETINGN.MD`). The full title is preserved in the `# Heading`
of each file and restored as `display_title` on load, so the application
always shows and searches the full user title. `[[Meeting Notes]]` wiki links
resolve correctly regardless of the underlying filename.

### Tested Environments

**DOSBox-X 2024.03.01** (`machine=svga_s3`, `memsize=16`, `lfn=true`,
`cycles=max`, LFN build): `memex.exe --smoke-test` and `--persistence-test`
pass; interactive TUI launches and operates correctly. Compiler: GCC 14.2.0
(`i686-pc-msdosdjgpp-gcc`), DJGPP 2.05, PDCursesMod DOS backend.

**DOSBox-X 2024.03.01** (`lfn=false`, non-FAT binary): binary starts without
SIGILL; 8.3 filename truncation is active and the smoke/persistence tests fail
as expected (confirmed need for FAT build). See `DOS_BUILD.md` for details.

FAT build on bare MS-DOS 6.22: pending.

### Known Missing or Reduced Features

The DOS profile (`-DMEMEX_DOS_PROFILE`) reduces compile-time limits:

| Limit              | Linux | DOS |
|--------------------|-------|-----|
| `MAX_NOTES`        | 512   | 128 |
| `MAX_LINES`        | 2048  | 1024 |
| `MAX_RENDERED`     | 8192  | 2048 |
| `MAX_RESULTS`      | 256   | 96 |
| `MAX_BACKLINKS`    | 256   | 96 |
| `MAX_MENTIONS`     | 256   | 96 |
| `MAX_DIRS`         | 256   | 96 |
| `MAX_SIDEBAR_ITEMS`| 1024  | 256 |
| `MAX_HISTORY`      | 128   | 32 |
| `MAX_SAVED_SEARCHES`| 32   | 16 |
| `MEMEX_PATH_MAX`   | 1024  | 260 |

Optional features can be disabled individually if memory or performance is
tight on the target machine:

| Define                          | Effect                                 |
|---------------------------------|----------------------------------------|
| `-DMEMEX_DISABLE_SAVED_SEARCHES`| Disables saved search persistence      |
| `-DMEMEX_DISABLE_MENTIONS`      | Disables unlinked mention indexing     |
| `-DMEMEX_DISABLE_TRANSCLUSION`  | Disables `![[Note]]` transclusion      |
| `-DMEMEX_DISABLE_MOUSE`         | Forces keyboard-only input             |

Arrow-key, page-up/down, home, and end behavior under PDCurses has not yet
been confirmed on a real DOS terminal; keyboard-only operation should work but
may need key-mapping adjustments after first-boot testing.

### Recommended Memory

The DOS profile static BSS is approximately 1.67 MB on the Linux host
(`-DMEMEX_DOS_PROFILE`). With DPMI overhead, stack, and a modest heap, a
machine with 4 MB of extended memory is the practical minimum; 8 MB provides
comfortable headroom. A 386SX/8 MB machine is the primary design target.

Host measurements with the DOS profile (not real DOS runtime):

```
case25:    notes=25   load=0.002 s   index_heap=40 KB
case100:   notes=100  load=0.022 s   index_heap=159 KB
case_max:  notes=128  load=0.036 s   index_heap=204 KB
large_note: lines=192  editor_load=0.000 s  static_edit_buffer=241 KB
```

Real DOS measurements will differ, especially on a 386SX with slower storage.

### Troubleshooting

**`No DPMI` error on startup** — CWSDPMI is not loaded. Place `CWSDPMI.EXE`
in the same directory as `memex.exe` or on the `PATH`. On FreeDOS, installing
the `DPMI` package provides it system-wide.

**`Cannot find PDCurses` at link time** — PDCurses was not built for the DJGPP
target, or `libpdcurses.a` is not in the library search path. Build PDCurses
from source with the same DJGPP toolchain and install it into the DJGPP prefix,
or pass `-L/path/to/pdcurses` in `LDFLAGS`.

**Terminal looks wrong or key input is broken** — PDCurses for DOS uses the
direct console API. If running in DOSBox-X, set the machine type to `386` and
enable `VESA` or `CGA` output. Ensure `COLS` and `LINES` reflect the actual
console size. If function or arrow keys are not working, check whether the
PDCurses build has `#define XCURSES` or the DOS-console backend selected, not
the X11 or SDL variant.

**Long filename errors on file create** — Running the LFN build without long
filename support. Under FreeDOS, load `DOSLFN.COM` or `JLFN.COM` first. Under
DOSBox-X, set `lfn=true` in `[dos]`. Alternatively, use the FAT build
(`make -f Makefile.dj fat`) which requires no LFN support.

**Memory allocation failures on startup** — The DOS profile limits are
aggressive enough for 8 MB but may need further reduction on a 4 MB machine.
Add `-DMEMEX_DISABLE_MENTIONS -DMEMEX_DISABLE_TRANSCLUSION` to `CFLAGS` and
rebuild to recover roughly 100–200 KB of index heap.

## Run

```sh
./memex ~/notes
```

If no directory is passed, `memex` uses the current directory. Missing note
directories are created.

## Keys

- `j/k` or arrows: move selection / editor cursor
- `Enter`: open selected note; on the already-open note, follow the selected link or chosen result
- `n`: create note
- `t`: create note from a named template file in `.templates/`
- `D`: open or create today's daily note
- `m`: toggle read/write note view
- `e`: edit open note
- `f`: full-text search note contents
- `o`: show heading outline for the open note
- `g`: browse/filter tags
- `u`: show unlinked mentions for the open note
- `i`: show note info for the open note
- `v`: open saved searches
- `p`: open command palette
- `r`: rename note
- `d`: move note to `.trash/`
- `/`: filter note titles
- `b`: show backlinks for the open note
- `A`: save the current full-text search
- `s`: show/hide the notes sidebar
- `S`: cycle sort mode (alphabetical, modified time, created time)
- `T`: cycle color scheme
- `[` / `]`: back/forward note history
- `Tab` / `Shift-Tab`: move link selection inside the open note
- `Esc`: leave editor/filter/backlinks
- `Ctrl-X`: save and exit editor
- `Ctrl-Z` / `Ctrl-Y`: undo / redo in editor
- `Ctrl-T`: toggle checkbox on the current editor line
- `Ctrl-F`: find within the current note while editing
- `Tab` in editor: autocomplete note titles inside `[[...]]`
- `Ctrl-C` or `q`: quit

## Mouse

Mouse input is optional and depends on the curses runtime and terminal/console
driver. When available:

- Wheel: scroll the active note/panel; over the sidebar, move the note selection
- Left click in the sidebar: select a note or folder
- Double-click in the sidebar: open the selected note or expand/collapse a folder
- Left click in result/palette/backlink/tag panels: select an item
- Double-click in those panels: activate the selected item
- Left click a note link line: select that link
- Double-click a note link line: follow that link
- In the editor, left click positions the cursor and the wheel scrolls lines

## Limits

The defaults are intentionally small:

- Maximum notes listed: 512
- Maximum note size loaded in editor/viewer: 48 KiB
- Maximum lines per note in editor: 2048
- Maximum line length: 240 bytes

These are compile-time constants defined in `memex_config.h`. The DOS build
profile (`-DMEMEX_DOS_PROFILE`) selects smaller values; see the DOS Build
section above for the full comparison.

Runtime state is stored in `.memex-state` inside the note directory, and
deleted notes are moved into `.trash/` under that same directory by default.
Optional templates live in `.templates/`, daily-note naming can be customized
with a strftime pattern in `.memex-daily-format`, saved searches live in
`.memex-searches`, and optional configuration can be provided in `.memexrc`.

## Configuration

`.memexrc` uses simple `key=value` lines inside the notes directory.

Supported options:

- `theme=plain|amber|forest|ocean`
- `default_mode=read|write`
- `startup=last|daily|none`
- `trash_dir=.trash`
- `template_dir=.templates`
- `key_new`, `key_template`, `key_daily`, `key_edit`, `key_delete`, `key_rename`
- `key_filter`, `key_search`, `key_backlinks`, `key_mentions`, `key_outline`
- `key_tags`, `key_saved`, `key_palette`, `key_info`
- `key_theme`
