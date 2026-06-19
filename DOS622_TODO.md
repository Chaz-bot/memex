# DOS 6.22 TODO

Action checklist for running `memex` on bare MS-DOS 6.22 without a long
filename driver. Builds on the Phase 12 DOSBox-X port. The two blockers are
DPMI (no built-in provider in DOS 6.22) and 8.3 filenames (no LFN on FAT16).

## Ground Rules

- Keep the Linux build working throughout.
- Keep the DOSBox-X build (`MEMEX_DOS_PROFILE` without `MEMEX_DOS_FAT`) working.
- Isolate all 8.3 changes behind a new `MEMEX_DOS_FAT` compile-time flag.
- Do not scatter `#ifdef` guards into `memex.c` logic beyond `sanitize_title`
  and `title_to_file`.
- All 8.3 filenames must fit: base ≤ 8 chars, extension ≤ 3 chars, no spaces,
  no leading dots, no DOS-illegal characters (`+`, `=`, `,`, `[`, `]`, `;`).

## Phase 1: DPMI Provider

- [x] Download `CWSDPMI.EXE` (CSDPMI5B.ZIP, public domain, C.W. Sandmann).
      Found r7 (2010) already present at `/home/chaz/dos-test/CWSDPMI.EXE`.
- [ ] Verify the binary starts `memex.exe` on bare DOS 6.22 or an emulator
      configured without built-in DPMI. (Requires real hardware or a boot
      image without a DPMI host; cannot be completed in DOSBox-X which always
      provides its own DPMI.)
- [x] Add `CWSDPMI.EXE` to the distribution package alongside `memex.exe`.
      Copied to repository root; deploy both files together.
- [x] Document in `DOS_BUILD.md`: on bare DOS 6.22, `CWSDPMI.EXE` must be in
      the same directory as `memex.exe` or on `PATH`.

## Phase 2: New `MEMEX_DOS_FAT` Compile Flag

- [x] Add `MEMEX_DOS_FAT` to `memex_config.h`; document that it implies
      `MEMEX_DOS_PROFILE` and selects 8.3-safe filenames.
- [x] Add a `check-fat` target to `Makefile.dj` that compiles with both
      `-DMEMEX_DOS_PROFILE` and `-DMEMEX_DOS_FAT` without linking (syntax
      check only, since PDCurses is not always available on the host).
- [x] Confirm the Linux build ignores `MEMEX_DOS_FAT` and continues to pass
      `make smoke` and `make persistence`.

## Phase 3: 8.3 Config And Special File Names

All current special file names use leading dots or exceed 8.3 limits. Under
`MEMEX_DOS_FAT`, replace them in `memex_config.h`:

| Constant | Current value | 8.3 replacement |
|---|---|---|
| `STATE_FILE` | `.memex-state` | `MXSTATE.DAT` |
| `CONFIG_FILE` | `.memexrc` | `MEMEXRC.CFG` |
| `SAVED_SEARCH_FILE` | `.memex-searches` | `MXSRCH.DAT` |
| `DAILY_FORMAT_FILE` | `.memex-daily-format` | `MXDAYFMT.DAT` |
| `TRASH_DIR` | `.trash` | `TRASH` |
| `TEMPLATE_DIR` | `.templates` | `TEMPLATE` |
| `REWRITE_TMP_FILE` | *(new constant)* | `MXRWRT.TMP` |

`DEFAULT_TEMPLATE` (`default.md`) and `DAILY_TEMPLATE` (`daily.md`) are
already 8.3-legal and need no change.

- [x] Add `#ifdef MEMEX_DOS_FAT` block to `memex_config.h` with the above
      8.3 values.
- [x] Add `REWRITE_TMP_FILE` constant for both profiles (`.memex-rewrite.tmp`
      for LFN builds, `MXRWRT.TMP` for FAT builds).
- [x] Replace the string literal `".memex-rewrite.tmp"` at `memex.c:2642`
      with `REWRITE_TMP_FILE`.
- [x] Confirm no other long special file names are hardcoded in `memex.c`
      (search for `\.memex`, `\.trash`, `\.templates`).

## Phase 4: 8.3 Note Filename Sanitization

Note filenames are produced by `sanitize_title()` (`memex.c:701`) and
`title_to_file()` (`memex.c:728`). Neither enforces 8.3 limits.

### sanitize_title

Under `MEMEX_DOS_FAT`, after the existing character substitutions:

- [x] Replace spaces with `_`.
- [x] Strip or replace additional 8.3-illegal characters: `+`, `=`, `,`,
      `[`, `]`, `;`, `@`, `!`, `#`, `$`, `%`, `^`, `&`, `(`, `)`.
      (Keep `-` and `_` as they are valid in 8.3.)
- [x] Truncate the result to 8 characters.
- [x] Re-run the trailing-dot/space strip after truncation.
- [x] Re-run the empty-title and reserved-name checks after truncation.

### title_to_file (collision deduplication)

Two distinct titles can produce the same 8-char stem. `title_to_file` must
avoid creating a note whose filename already exists:

- [x] After building the candidate `NAME.MD`, check whether the file exists
      in the note directory using `platform_file_exists`.
- [x] If it exists, try `NAME~1.MD`, `NAME~2.MD`, … `NAME~9.MD` (truncate
      the base to 6 chars to make room for the `~N` suffix).
- [x] If all suffixes are taken, return an error so the caller can surface
      a message to the user.
- [x] Apply the same deduplication in the rename path (`memex.c` around the
      call that derives the new filename from the renamed title).

## Phase 5: Nested Note Directory Names

`sanitize_rel_title()` (`memex.c:2358`) calls `sanitize_title()` for each
path segment, so the Phase 4 changes propagate to nested notes automatically.
Verify explicitly:

- [x] A note title containing `/` as a nesting separator still splits into
      segments before truncation (each segment ≤ 8 chars independently).
      `sanitize_rel_title` nulls out at each `/` and calls `sanitize_title`
      per segment, so the Phase 4 truncation applies independently per segment.
- [x] Nested note directories created by `ensure_parent_dirs` (`memex.c:2472`)
      are themselves 8.3-legal after sanitization. `ensure_parent_dirs`
      receives an already-sanitized `rel_path` and calls `platform_mkdir` on
      each path prefix without re-sanitizing.

## Phase 6: Display Title Roundtrip

Under 8.3, a note titled "Meeting Notes" is stored as `MEETINGN.MD` (or
similar). On reload, `note->title` is derived from the filename stem and
`display_title` is set from the `# heading` or YAML `title:` field. The
existing `find_note_by_target` checks both fields, so `[[Meeting Notes]]` will
resolve correctly. Verify the following hold under `MEMEX_DOS_FAT`:

- [x] `write_note_template` writes a `# Title` heading for every new note so
      `display_title` survives the 8.3 stem on the next load.
      `create_note_with_template` now derives the heading from the original
      title input's last segment rather than the sanitized 8.3 stem.
- [x] `find_note_by_target` resolves `[[Full Title]]` links for notes stored
      under truncated 8.3 filenames. It already checks both `title` (stem)
      and `display_title`; with the heading fix above, `display_title` is
      set to the full user title on the first load.
- [x] Rename link rewriting rewrites `[[Old Full Title]]` to
      `[[New Full Title]]` correctly even when the underlying filenames are
      8.3-truncated. `rewrite_file_links` now takes `old_display`/`new_display`
      params and matches links written with the full display title in addition
      to the filename stem. `rename_current_note` derives `new_display` from
      the user's rename input and passes both pairs to `rewrite_links_for_rename`.
      Autocomplete now inserts `display_title` as the link text so links use
      the full user title rather than the 8.3 stem.
- [x] The title filter and full-text search operate on `display_title` and
      `rel_path`, not the 8.3 stem, so long titles remain searchable.
      The filter already checks `title`, `display_title`, and `rel_path`
      (line 417-419); full-text search scans file content which includes
      the `# heading` written with the full title.

## Phase 7: Update Smoke And Persistence Tests

Several test note names are either too long or contain spaces, which are
illegal in 8.3. Under `MEMEX_DOS_FAT`, update the test scaffolding:

| Current test name | Problem | Suggested 8.3 name |
|---|---|---|
| `Old Name` | space | `Old-Name` |
| `Renamed Note` | space, 11 chars | `Renamed` |
| `Mentioner` | 9 chars | `Mention` |
| `Templated` | 9 chars | `Tmplated` |

- [x] Add `#ifdef MEMEX_DOS_FAT` variants of affected test note names and
      link text in `run_smoke_tests` and `run_persistence_tests`.
- [x] Confirm `make smoke` and `make persistence` pass with `-DMEMEX_DOS_FAT`
      on the Linux host.
- [ ] Confirm `memex.exe --smoke-test c:\smoke` passes on the target
      DOS 6.22 environment.
- [ ] Confirm `memex.exe --persistence-test c:\persist` passes on the target
      DOS 6.22 environment.

## Phase 8: Build System

- [x] Add `fat` target to `Makefile.dj` that builds `memex.exe` with both
      `-DMEMEX_DOS_PROFILE` and `-DMEMEX_DOS_FAT`.
- [x] Add `check-fat` target (compile without link, as with `check-syntax`).
      (Already added in Phase 2; confirmed present and passing.)
- [x] Confirm `make -f Makefile.dj check-fat` passes on the Linux host with
      the DJGPP cross compiler and PDCursesMod headers.
- [x] Record exact compiler, PDCurses, and DOS runtime versions used for the
      FAT build in `DOS_BUILD.md`.

## Phase 9: Runtime Verification On DOS 6.22

- [ ] Boot bare DOS 6.22 (real hardware or emulator with DOS 6.22 boot image
      and no built-in DPMI or LFN support).
- [ ] Confirm `CWSDPMI.EXE` loads and `memex.exe` starts without `SIGILL` or
      DPMI errors.
- [ ] Confirm `memex.exe --smoke-test c:\smoke` passes.
- [ ] Confirm `memex.exe --persistence-test c:\persist` passes.
- [ ] Confirm interactive TUI launches and keyboard navigation works.
- [ ] Confirm create, edit, save, rename, and trash note operations work with
      8.3 filenames on the FAT filesystem.
- [ ] Confirm link following, backlinks, tags, outline, and search work.
- [ ] Confirm state and config persistence across restarts using the 8.3 file
      names.
- [ ] Confirm there are no collisions or truncation surprises at `MAX_NOTES`
      note count.
- [ ] Record tested DOS version, DPMI provider version, and hardware or
      emulator version in `DOS_BUILD.md`.

## Phase 10: Documentation

- [ ] Update `DOS_BUILD.md` with `MEMEX_DOS_FAT` flag, 8.3 file name table,
      and CWSDPMI bundling instructions.
- [ ] Update `DOS_PORT.md` to describe the FAT filename scheme and the display
      title roundtrip.
- [ ] Update `README.md` DOS section with DOS 6.22 requirements.
- [ ] Note the UX implication: note filenames are stored in 8.3 format, but
      titles from `# headings` or YAML frontmatter display correctly at full
      length.
