# DOS Port TODO

Action checklist for porting `memex` to DOS while keeping the Linux build
working. The first target is DJGPP plus PDCurses on a 386SX / 8 MB class
machine.

## Ground Rules

- [ ] Keep the DOS work on the `dos-port` branch.
- [ ] Keep Linux behavior buildable while platform code is introduced.
- [ ] Prefer a small platform layer over scattered DOS-specific conditionals.
- [ ] Target 32-bit protected-mode DOS first, not 16-bit real-mode DOS.
- [ ] Require long filename support for the first working port.
- [ ] Defer feature trimming until after there is a measurable DOS build.

## Phase 1: Establish The DOS Build Target

- [ ] Install or identify a DJGPP build environment.
- [ ] Install or build PDCurses for the same DJGPP environment.
- [x] Confirm the expected DOS runtime target:
  - [x] FreeDOS, MS-DOS, or emulator.
  - [x] DPMI provider available.
  - [x] Long filename support available.
  - [x] Mouse driver expectations, if mouse support matters.
- [ ] Document exact compiler, curses, and DOS runtime versions.
- [x] Add a small `build-dos.bat` or `Makefile.dj` for the DOS build.
- [x] Add `-DMEMEX_DOS_PROFILE` to the DOS build flags.
- [x] Link the DOS build against PDCurses.
- [x] Keep the existing Linux `Makefile` path intact.

## Phase 2: Split Configuration Limits

- [x] Move compile-time limits out of `memex.c` into a config header.
- [x] Add a normal profile matching the current Linux limits.
- [x] Add a DOS profile with smaller limits:
  - [x] `PATH_MAX`: `1024 -> 260`
  - [x] `MAX_NOTES`: `512 -> 128`
  - [x] `MAX_LINES`: `2048 -> 1024`
  - [x] `MAX_RENDERED`: `8192 -> 2048`
  - [x] `MAX_RESULTS`: `256 -> 96`
  - [x] `MAX_BACKLINKS`: `256 -> 96`
  - [x] `MAX_MENTIONS`: `256 -> 96`
  - [x] `MAX_DIRS`: `256 -> 96`
  - [x] `MAX_SIDEBAR_ITEMS`: `1024 -> 256`
  - [x] `MAX_HISTORY`: `128 -> 32`
  - [x] `MAX_SAVED_SEARCHES`: `32 -> 16`
- [x] Build and run the Linux build after moving the constants.
- [x] Measure static/global memory usage for both profiles.

## Phase 3: Add A Platform Layer

- [x] Add `platform.h`.
- [x] Add `platform_posix.c`.
- [x] Add `platform_dos.c`.
- [x] Move current directory lookup behind `platform_getcwd`.
- [x] Move directory creation behind `platform_mkdir`.
- [x] Move file existence checks behind `platform_file_exists`.
- [x] Move directory checks behind `platform_is_dir`.
- [x] Move rename operations behind `platform_rename`.
- [x] Add `platform_path_sep`.
- [x] Add path normalization helpers.
- [x] Add a directory iterator API:
  - [x] `platform_opendir`
  - [x] `platform_readdir`
  - [x] `platform_closedir`
- [x] Replace direct `opendir`, `readdir`, and `closedir` usage.
- [x] Replace direct `getcwd`, `mkdir`, `stat`, and `rename` usage.
- [x] Keep POSIX behavior identical after wrapper replacement.

## Phase 4: Normalize Paths And Names

- [x] Centralize path joining.
- [x] Normalize `/` and `\` handling.
- [x] Audit every direct string append involving paths.
- [x] Confirm nested note directories work through the platform layer.
- [x] Decide DOS names for dot-prefixed support files:
  - [x] `.memex-state`
  - [x] `.memexrc`
  - [x] `.memex-searches`
  - [x] `.memex-daily-format`
  - [x] `.trash`
  - [x] `.templates`
- [x] Either keep dot names under long filename DOS or add DOS-safe aliases.
- [x] Audit filename validation for reserved DOS device names.
- [x] Audit maximum path and component length handling.

## Phase 5: Isolate Curses Usage

- [x] Add a curses compatibility header, such as `ui_curses.h`.
- [x] Include curses through the compatibility header only.
- [x] Centralize color initialization.
- [x] Centralize keyboard normalization.
- [x] Normalize Enter, Backspace, Tab, Shift-Tab, Escape, and function-key behavior.
- [ ] Confirm arrow, page up, page down, home, and end keys under PDCurses.
- [x] Gate mouse support behind a compile-time capability check.
- [x] Confirm keyboard-only operation works without mouse support.
- [x] Keep Linux ncurses behavior unchanged.

## Phase 6: First DOS Compile

- [ ] Build with DJGPP and PDCurses.
- [x] Fix compiler errors without changing user-facing behavior.
- [x] Fix missing headers and incompatible declarations.
- [ ] Fix C library differences found by DJGPP.
- [ ] Confirm the executable starts and exits cleanly.
- [ ] Confirm startup creates or opens the note directory.
- [ ] Confirm no large unexpected allocations fail at startup.
- [ ] Record executable size and available memory.

## Phase 7: Core Runtime Smoke Tests

- [x] Open an empty note directory.
- [x] Create a note.
- [x] Edit and save a note.
- [x] Reopen the saved note.
- [x] Create a nested directory note if nested folders remain enabled.
- [x] Rename a note.
- [x] Trash a note.
- [x] Follow a simple `[[wiki link]]`.
- [x] Follow an aliased `[[Note|Label]]` link.
- [x] Follow a heading `[[Note#Heading]]` link.
- [x] Search by title.
- [x] Run full-text search.
- [x] Open backlinks.
- [x] Open tags.
- [x] Open outline.
- [x] Open command palette.
- [ ] Quit with and without unsaved changes.

## Phase 8: Persistence And Config Tests

- [ ] Save and restore `.memex-state`.
- [ ] Save and restore sidebar visibility.
- [ ] Save and restore read/write mode.
- [ ] Load `.memexrc`.
- [ ] Confirm keybinding overrides work.
- [ ] Confirm theme selection works.
- [ ] Confirm saved searches load and save.
- [ ] Confirm templates load from the template directory.
- [ ] Confirm daily note naming works.

## Phase 9: Performance And Memory Pass

- [ ] Test with 25 notes.
- [ ] Test with 100 notes.
- [ ] Test with maximum DOS-profile note count.
- [ ] Test with a large note near `MAX_NOTE_BYTES`.
- [ ] Measure startup time.
- [ ] Measure note switching time.
- [ ] Measure full-text search time.
- [ ] Measure memory after indexing.
- [ ] Measure memory while editing a large note.
- [ ] Reduce limits further if the target machine is unstable.
- [ ] Consider lazy indexing if startup or memory is too expensive.

## Phase 10: Feature Triage If Needed

- [ ] Make saved searches optional if memory is tight.
- [ ] Make unlinked mentions optional if indexing is too expensive.
- [ ] Reduce backlink and mention limits if needed.
- [ ] Reduce rendered line cache size if needed.
- [ ] Reduce full-text search result count if needed.
- [ ] Reduce folder sidebar detail if needed.
- [ ] Consider disabling transclusion in the DOS profile if recursion or memory is problematic.
- [ ] Consider disabling mouse support if it complicates PDCurses portability.

## Phase 11: Documentation

- [ ] Update `README.md` with DOS build status.
- [ ] Add DOS build prerequisites.
- [ ] Add DOS build commands.
- [ ] Add DOS runtime requirements.
- [ ] Document long filename requirement.
- [ ] Document tested DOS environments.
- [ ] Document known missing or reduced features.
- [ ] Document recommended memory limits.
- [ ] Document troubleshooting for PDCurses and DPMI errors.

## Phase 12: Release Criteria

- [ ] Linux build still passes a basic smoke test.
- [ ] DOS build produces an executable.
- [ ] DOS executable runs on the intended machine or emulator.
- [ ] Keyboard navigation works.
- [ ] Create, edit, save, rename, trash, and reopen notes work.
- [ ] Links, backlinks, tags, outline, and search work at DOS-profile limits.
- [ ] State and config persistence work.
- [ ] Performance is acceptable on the target hardware.
- [ ] Memory use is stable after repeated open/edit/search cycles.
- [ ] `DOS_PORT.md` matches the implemented design.
- [ ] `DOS_TODO.md` reflects remaining known work.
