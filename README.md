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
- Toggle between read mode with wrapped rendered markdown and write mode with raw markdown.
- Search note titles.
- Full-text search across note contents with jumpable results.
- Follow `[[wiki links]]`, including `[[Note|Alias]]` and `[[Note#Heading]]`.
- Show backlinks and heading outlines on demand.
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
- `r`: rename note
- `d`: move note to `.trash/`
- `/`: filter note titles
- `b`: show backlinks for the open note
- `s`: show/hide the notes sidebar
- `S`: cycle sort mode (alphabetical, modified time, created time)
- `[` / `]`: back/forward note history
- `Tab` / `Shift-Tab`: move link selection inside the open note
- `Esc`: leave editor/filter/backlinks
- `Ctrl-X`: save and exit editor
- `Ctrl-Z` / `Ctrl-Y`: undo / redo in editor
- `Ctrl-T`: toggle checkbox on the current editor line
- `Ctrl-F`: find within the current note while editing
- `Ctrl-C` or `q`: quit

## Limits

The defaults are intentionally small:

- Maximum notes listed: 512
- Maximum note size loaded in editor/viewer: 48 KiB
- Maximum lines per note in editor: 2048
- Maximum line length: 240 bytes

These are compile-time constants near the top of `memex.c`.

Runtime state is stored in `.memex-state` inside the note directory, and
deleted notes are moved into `.trash/` under that same directory. Optional
templates live in `.templates/`, and daily-note naming can be customized with a
strftime pattern in `.memex-daily-format`.
