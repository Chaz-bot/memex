# memex

`memex` is an Obsidian-lite terminal note browser/editor for old Linux systems.
It is written as one small C89-ish program using curses, intended to build on
Slackware 4.0 and run acceptably on a Pocket 386 class machine with a 386SX and
8 MB of RAM.

## Features

- Plain `.md` notes in nested folders.
- Vim-like movement keys plus arrow keys where curses supports them.
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

## Limits

The defaults are intentionally small:

- Maximum notes listed: 512
- Maximum note size loaded in editor/viewer: 48 KiB
- Maximum lines per note in editor: 2048
- Maximum line length: 240 bytes

These are compile-time constants near the top of `memex.c`.

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
