# DOS Build Notes

This branch targets a first DOS port using DJGPP and PDCurses. The goal is a
32-bit protected-mode DOS executable before attempting any 16-bit compiler or
real-mode memory model.

## Target

- Compiler: DJGPP GCC
- UI library: PDCurses for DOS
- Memory model: 32-bit protected mode with a DPMI provider
- Initial runtime: FreeDOS, MS-DOS, or an emulator with DPMI and long filename support
- Long filenames: required for the first working port
- Mouse: optional; keyboard-only operation must remain usable

The exact tested compiler, PDCurses, DOS runtime, DPMI provider, and machine or
emulator versions should be filled in after the first successful DOS build.

## Build

From a DJGPP environment with PDCurses installed:

```bat
build-dos.bat
```

or:

```bat
make -f Makefile.dj
```

The DOS build defines:

```text
MEMEX_DOS_PROFILE
```

and links against:

```text
pdcurses
```

## Current Status

Phase 1 establishes the DOS build entry points and target assumptions. The
source still uses POSIX headers and direct curses calls, so the DOS build is not
expected to compile cleanly until the platform and curses compatibility layers
are added in later phases.

Phase 2 adds `memex_config.h` and makes `MEMEX_DOS_PROFILE` select smaller
compile-time limits. On the Linux host compiler, the normal profile builds with
about 13.4 MB of BSS, while the DOS profile builds with about 1.67 MB of BSS.

Phase 3 adds `platform.h`, `platform_posix.c`, and `platform_dos.c`. The Linux
build links `platform_posix.c`; the DJGPP build links `platform_dos.c`. Direct
directory traversal, current-directory lookup, directory creation, stat checks,
and rename calls from `memex.c` now go through the platform layer.

Phase 4 keeps note-relative paths normalized with `/` internally and converts
to the platform separator when building filesystem paths. DOS still requires
long filename support for the first port, so dot-prefixed support names such as
`.memex-state`, `.memexrc`, `.trash`, and `.templates` are intentionally kept.
New note titles are sanitized for DOS-reserved device names and trailing
dot/space characters.

Phase 5 adds `ui_curses.h` as the only direct curses include. Key input is
normalized there for Enter, Backspace, Tab, Shift-Tab, Escape, arrows, page
navigation, and mouse events. Color setup also goes through the compatibility
header. Mouse support is gated by curses capability detection and can be forced
off with `-DMEMEX_DISABLE_MOUSE` for keyboard-only builds.

Phase 6 source validation passes with:

```sh
make -f Makefile.dj check-syntax
```

That target compiles `memex.c` and `platform_dos.c` with `MEMEX_DOS_PROFILE`
without linking PDCurses, which is useful on hosts that do not have the DOS
library installed. A full `make -f Makefile.dj` currently requires a real DJGPP
environment with PDCurses available. In this workspace, the full DOS build gets
through compilation and stops at link because `-lpdcurses` is missing.

For a Linux-hosted DJGPP cross compiler, pass the compiler explicitly, for
example:

```sh
make -f Makefile.dj CC=i586-pc-msdosdjgpp-gcc
```

Phase 7 adds a noninteractive core runtime smoke test:

```sh
./memex --smoke-test /tmp/memex-smoke
```

On the Linux host this is also available through:

```sh
make smoke
```

The smoke test creates notes in the supplied directory, exercises open/create,
save/reopen, nested notes, link following, title filtering, full-text search,
backlinks, mentions, tags, outline, command-palette state, rename link rewrite,
and trash behavior. It does not initialize curses, so it can run in a DOS
environment once `memex.exe` links.

Phase 8 adds a noninteractive persistence/config test:

```sh
./memex --persistence-test /tmp/memex-persistence
```

On the Linux host this is also available through:

```sh
make persistence
```

The persistence test writes and reloads `.memex-state`, `.memexrc`, saved
searches, template files, and the daily-note format file. It verifies sidebar
visibility, read/write mode, theme/config/key overrides, saved-search
persistence, template expansion, and daily note naming.

Phase 9 adds a noninteractive performance/memory pass:

```sh
./memex --performance-test /tmp/memex-performance
```

On the Linux host this is also available through:

```sh
make performance
```

The performance test generates 25-note, 100-note, and `MAX_NOTES` data sets,
then reports load/index time, repeated note-switch time, full-text search time,
indexed heap bytes, loaded-view heap bytes, and result counts. It also creates a
large note near `MAX_NOTE_BYTES` and reports editor-load time, the static editor
buffer size, and loaded-view heap use. On the host DOS-profile build, the max
case uses the DOS `MAX_NOTES` value.

Lazy indexing was considered after the host DOS-profile run. The current
measurements do not justify adding that complexity before testing on real
DJGPP/PDCurses hardware or emulation, so no lazy-indexing change has been made
yet.

Most recent host DOS-profile measurements:

```text
case25:  notes=25  load=0.002s switch20=0.000s search=0.000s index_heap=40722
case100: notes=100 load=0.022s switch20=0.000s search=0.000s index_heap=163122
case_max: notes=128 load=0.036s switch20=0.001s search=0.000s index_heap=208818
large_note: lines=192 editor_load=0.000s static_edit_buffer=246784 view_heap=9009
```

These are Linux-host measurements with `-DMEMEX_DOS_PROFILE`, not real DOS
runtime measurements.
