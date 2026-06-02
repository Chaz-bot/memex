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
