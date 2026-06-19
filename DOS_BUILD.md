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

## DPMI On Bare MS-DOS 6.22

MS-DOS 6.22 has no built-in DPMI provider. `memex.exe` is built with the DJGPP
go32 v2.05 stub, which automatically loads `CWSDPMI.EXE` at startup when no
DPMI host is detected.

`CWSDPMI.EXE` (r7, 2010, C.W. Sandmann, public domain) is included in this
repository. Place it in the same directory as `memex.exe`:

```
C:\MEMEX\
    MEMEX.EXE
    CWSDPMI.EXE
```

The go32 stub searches the executable's own directory before `PATH`, so no
`PATH` change or `AUTOEXEC.BAT` entry is required.

If `CWSDPMI.EXE` is missing and DOS has no DPMI provider, the stub prints:

```
DPMI not found
```

and exits without running `memex.exe`.

`CWSDPMI.EXE` is not needed under DOSBox-X (built-in DPMI) or FreeDOS with
`HDPMI32.EXE` or another DPMI server already loaded.

## FAT Build for MS-DOS 6.22 (`MEMEX_DOS_FAT`)

`MEMEX_DOS_FAT` extends `MEMEX_DOS_PROFILE` for bare MS-DOS 6.22 FAT16
filesystems without a long filename driver. It replaces all config, state,
and note filenames with 8.3-safe equivalents.

Use the `fat` target:

```sh
make -f Makefile.dj fat \
  CC=i686-pc-msdosdjgpp-gcc \
  LIBS="/path/to/PDCursesMod/dos/pdcurses.a" \
  CFLAGS="-O2 -Wall -march=i386 -DMEMEX_DOS_PROFILE -DMEMEX_DISABLE_MOUSE -I/path/to/PDCursesMod"
```

The `fat` target appends `-DMEMEX_DOS_FAT` to `CFLAGS` automatically. The
compiler, PDCurses library, C runtime, and all other build flags are identical
to the standard DOS build documented above (GCC 14.2.0, DJGPP 2.05,
PDCursesMod DOS backend, `djgpp-djcrx-bootstrap 2.05-5`).

The `check-fat` target verifies compilation without linking — useful on Linux
hosts that do not have the DOS PDCurses library installed:

```sh
make -f Makefile.dj check-fat
```

This passes on the Linux host with the DJGPP cross compiler and PDCursesMod
headers.

### Filename mapping under `MEMEX_DOS_FAT`

| Purpose | Standard (LFN) name | 8.3 name |
|---|---|---|
| State | `.memex-state` | `MXSTATE.DAT` |
| Config | `.memexrc` | `MEMEXRC.CFG` |
| Saved searches | `.memex-searches` | `MXSRCH.DAT` |
| Daily format | `.memex-daily-format` | `MXDAYFMT.DAT` |
| Trash directory | `.trash` | `TRASH` |
| Template directory | `.templates` | `TEMPLATE` |
| Rewrite scratch | `.memex-rewrite.tmp` | `MXRWRT.TMP` |

Note files use a sanitized title stem (≤ 8 chars, `.MD` extension). If two
titles produce the same 8-char stem, `~1` through `~9` suffixes are tried
(Windows-style short name collision). The full user title is written as a
`# Heading` in each note file and restored as `display_title` on load, so
link resolution and search operate on the full title regardless of what the
filename looks like on disk.

## DOS 6.22 Runtime Testing

### Test harness

`dos622-test.conf` and `runtest.bat` are included in the repository root.
Edit the mount paths in `dos622-test.conf` and run:

```sh
dosbox-x -conf dos622-test.conf -silent -exit
```

Or, from a real DOS 6.22 prompt with the repo files on `C:` and scratch
directories on `S:` and `P:`:

```bat
RUNTEST
```

### Non-FAT binary on LFN-disabled DOSBox-X (findings, 2026-06-19)

Tested with DOSBox-X 2024.03.01, `lfn=false`, `machine=svga_s3`, `memsize=16`,
`cycles=max`, and the existing (non-FAT) `memex.exe` to document the problem
the FAT build is intended to solve.

**Observed:**

- Binary starts without `SIGILL` or DPMI errors. DOSBox-X provides its own
  DPMI host so `CWSDPMI.EXE` is not invoked in this configuration.
- 8.3 truncation is active for all filenames created by the non-FAT binary:
  `Mentioner.md` → `MENTIONE.MD`, `Persisted.md` → `PERSISTE.MD`.
- Nested note directory `PROJECTS/` is created correctly.
- State file `.memex-state` is created under a DOSBox-X 8.3-mapped name
  instead of the expected `MXSTATE.DAT` (the FAT build uses `MXSTATE.DAT`).
- Non-FAT smoke test fails at the first title comparison after file creation:
  `find_note_by_target("Mentioner")` returns -1 because the file on disk is
  `MENTIONE.MD` and the non-FAT binary's note title is derived from the
  8.3 stem `"MENTIONE"`, not `"Mentioner"`.
- Non-FAT persistence test saves `last_note=persiste` (8.3 stem of `Persisted`)
  which fails the `strcmp(last_open_title, "Persisted") == 0` check.

These failures confirm exactly the behaviour `MEMEX_DOS_FAT` is designed to
fix. With the FAT build:
- All note titles are pre-sanitized to ≤ 8 chars before file creation, so
  `title` and display_title comparisons stay in sync.
- Config and state files use the 8.3 names defined in `memex_config.h`.

**CWSDPMI note:** DOSBox-X provides built-in DPMI so `CWSDPMI.EXE` is never
loaded in this configuration. To test `CWSDPMI.EXE`, you need real MS-DOS 6.22
hardware or a DPMI-free emulator. The go32 stub searches the executable's own
directory and loads `CWSDPMI.EXE` automatically when no DPMI host is detected.

### FAT binary on LFN-disabled DOSBox-X (findings, 2026-06-19)

Built using the `andrewwutw/build-djgpp` v3.4 pre-built Linux x86_64 tarball
(GCC 12.2.0, `i586-pc-msdosdjgpp-gcc`) and PDCursesMod v4.5.4 DOS backend:

```sh
export PATH="$HOME/djgpp/djgpp/bin:$PATH"
make -f Makefile.dj fat \
  CC=i586-pc-msdosdjgpp-gcc \
  LIBS="$HOME/pdcursesmod/dos/pdcurses.a" \
  CFLAGS="-O2 -Wall -march=i386 -DMEMEX_DOS_PROFILE -DMEMEX_DOS_FAT \
          -DMEMEX_DISABLE_MOUSE -I$HOME/pdcursesmod"
```

Produced `memex.exe` (407 KB, DJGPP go32 DOS extender).

Tested with DOSBox-X 2024.03.01, `lfn=false`, `machine=svga_s3`, `memsize=16`,
`cycles=max`, `-date-host-forced`, mounted Linux directories as DOS drives.

**Results:**

- `memex.exe --smoke-test S:\` → **`smoke: PASS`**
- `memex.exe --persistence-test P:\` → **`persistence: PASS`**

Confirmed output files from smoke test:
- `ALPHA.MD`, `TARGET.MD`, `MENTION.MD` (8.3-safe note names)
- `PROJECTS/NESTED.MD` (nested directory)
- `REF.MD` (link rewrite from `[[Old-Name]]` to `[[Renamed]]` confirmed)
- `TRASH/RENAMED.MD` (trash operation confirmed)
- `MXSTATE.DAT` (8.3 state filename confirmed)

Confirmed output files from persistence test:
- `MXSTATE.DAT`, `MEMEXRC.CFG`, `MXDAYFMT.DAT`, `MXSRCH.DAT`
- `TMPLATED.MD` (template expansion: `# Tmplated From Template`)
- `PERSISTE.MD` (note with `# Persisted` heading; display_title roundtrip confirmed)
- `.TPL/` (custom template directory)
- `LOG/` (daily note directory)

**Runtime bugs found and fixed during FAT testing:**

Three bugs in `memex.c` only manifest under a real DOS 8.3 environment where
`readdir` returns filenames in a case that may differ from how the file was
created (DOSBox-X local-mount drives fold filenames to lowercase or uppercase
depending on the API path):

1. **`find_note_by_target` case sensitivity**: The function compared `title`
   (the 8.3 filename stem) with `strcmp`. Under DOS, readdir may return
   `"alpha"` for a file created as `"Alpha.MD"`, causing `strcmp("alpha",
   "Alpha")` to fail. Fixed by using `case_equals` for the `title` comparison;
   `display_title` (set from the `# heading`) keeps the original case and is
   still compared with `strcmp`.

2. **`smoke_sidebar_has_title` case sensitivity**: Same root cause — the
   helper compared `notes[i].title` with `strcmp`. Fixed by using `case_equals`
   for `title` and adding a `display_title` fallback.

3. **Trash and template directory exclusion in `scan_notes_recursive`**: The
   directory-scan loop used `strcmp` to skip the trash and template directories
   by name (`"TRASH"` vs `"trash"`, etc.). Fixed by using `case_equals` in both
   the note scan and the link-rewrite scan.

**Note on stdout capture:** DJGPP's go32 protected-mode extender does not
inherit the DOS stdout file handle from COMMAND.COM on the first program
invocation in a session. `printf("smoke: PASS\n")` may not appear in a `>>`
redirect if the smoke test is the first program run. `fflush(stdout)` is called
after each PASS print. Testing with a pre-existing redirect file or running the
persistence test first confirms both results in the redirect file.

**Note on DOS date:** Without a real-time clock sync, DOSBox-X may present an
incorrect system date to DJGPP. The daily note filename derived from
`strftime("%Y%m%d")` may not match the calendar date. This is a DOSBox-X
configuration issue, not a `memex` bug. Use `-date-host-forced` to sync the
host date.

**CWSDPMI note:** DOSBox-X provides built-in DPMI; `CWSDPMI.EXE` is not loaded
in this configuration. Testing `CWSDPMI.EXE` on bare MS-DOS 6.22 without a
built-in DPMI host is still pending.
