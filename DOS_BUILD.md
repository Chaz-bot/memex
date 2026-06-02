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
library installed.

A confirmed full build was produced using the following environment:

- **Compiler**: `i686-pc-msdosdjgpp-gcc` (GCC 14.2.0, AUR `djgpp-gcc 14.2.0-1`)
- **C runtime**: `djgpp-djcrx-bootstrap 2.05-5` (bootstrap package; the full
  `djgpp-djcrx` does not build with GCC 14 due to a `sortsyms` declaration
  conflict in the djasm assembler source)
- **PDCurses**: PDCursesMod DOS backend (Bill-Gray/PDCursesMod, `dos/` subdirectory)
- **Build command**:

```sh
make -f Makefile.dj \
  CC=i686-pc-msdosdjgpp-gcc \
  LIBS="/path/to/PDCursesMod/dos/pdcurses.a" \
  CFLAGS="-O2 -Wall -march=i386 -DMEMEX_DOS_PROFILE -DMEMEX_DISABLE_MOUSE -I/path/to/PDCursesMod"
```

Key build findings:

- `-march=i386` is required for both `memex.exe` and PDCurses. The default
  `i686-pc-msdosdjgpp-gcc` target emits i686 instructions (`cmov` etc.) that
  cause `SIGILL` on any CPU emulated below Pentium Pro level.
- `-DMEMEX_DISABLE_MOUSE` is required. PDCursesMod's `getmouse()` takes no
  arguments and returns `mmask_t`, which is incompatible with the ncurses
  `getmouse(MEVENT *)` API used in `memex.c`.
- PDCursesMod builds `pdcurses.a` without the `lib` prefix, so `-lpdcurses`
  does not resolve; pass the full path to `pdcurses.a` instead.
- The AUR `djgpp-gcc` package installs as `i686-pc-msdosdjgpp-gcc`, not
  `i586-pc-msdosdjgpp-gcc`.

Produced executable: `memex.exe`, 340 KB, DJGPP go32 v2.05 stub.

For a Linux-hosted DJGPP cross compiler:

```sh
make -f Makefile.dj CC=i686-pc-msdosdjgpp-gcc ...
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

Phase 10 adds optional feature-triage switches. All features remain enabled by
default, including in `MEMEX_DOS_PROFILE`, because the host DOS-profile
measurements do not justify reducing behavior before real DOS runtime testing.
If hardware or emulator testing shows memory, indexing, recursion, or input
problems, these switches can be added to `CFLAGS`:

```text
-DMEMEX_DISABLE_SAVED_SEARCHES
-DMEMEX_DISABLE_MENTIONS
-DMEMEX_DISABLE_TRANSCLUSION
-DMEMEX_DISABLE_MOUSE
```

The compact configuration can be checked on the Linux host with:

```sh
make triage
```

and source-checked for the DOS build with:

```sh
make -f Makefile.dj check-triage
```

The existing DOS-profile limits for backlinks, mentions, rendered lines,
results, and sidebar items were reviewed after Phase 9 and left unchanged.

Phase 12 confirms the following in DOSBox-X (dosbox-x-sdl2 2026.05.02,
machine=svga_s3, memsize=16, lfn=true, cycles=max):

- `memex.exe --smoke-test c:\smoke` passes.
- `memex.exe --persistence-test c:\persist` passes.
- `memex.exe c:\smoke` launches the interactive TUI without error.

Two source fixes were required for correct DOS runtime behavior:

1. `has_md_suffix` was case-sensitive and did not recognise `.MD` filenames
   returned by the FAT directory scanner when LFN is not active. Fixed to
   check each character case-independently.
2. When a note file has no YAML frontmatter, the title was derived solely from
   the filename. On a FAT filesystem without LFN, `Alpha.md` is stored and
   returned as `ALPHA.MD`, making the derived title `ALPHA` rather than
   `Alpha`. Fixed `parse_frontmatter` to read a leading `# Heading` line as
   `display_title` when no YAML block is present. `find_note_by_target` checks
   `display_title`, so the heading-derived title is used for link resolution
   and smoke test verification regardless of filename case.

DOSBox-X configuration used for testing (`dosbox-x.conf`):

```ini
[sdl]
windowresolution=1024x768
output=opengl

[dosbox]
machine=svga_s3
memsize=16

[dos]
lfn=true

[cpu]
cputype=pentium
cycles=max

[autoexec]
mount c /path/to/dos-test
c:
```

Note: DOSBox-X has built-in DPMI support; `CWSDPMI.EXE` is not required when
running under DOSBox-X. The `lfn=true` option (not `long file names=true`) is
the correct key for the `[dos]` section.
