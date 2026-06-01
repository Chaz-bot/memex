# memex

`memex` is an Obsidian-lite terminal note browser/editor for old Linux systems.
It is written as one small C89-ish program using curses, intended to build on
Slackware 4.0 and run acceptably on a Pocket 386 class machine with a 386SX and
8 MB of RAM.

## Features

- Plain `.md` notes in one directory.
- Vim-like movement keys plus arrow keys where curses supports them.
- Create, rename, delete, and edit notes.
- Toggle between read mode with rendered markdown and write mode with raw markdown.
- Search note titles.
- Follow `[[wiki links]]`.
- Show backlinks by scanning notes on demand.
- Tiny line-based editor with bounded memory use.

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
- `Enter`: open selected note; on the already-open note, follow selected link
- `n`: create note
- `m`: toggle read/write note view
- `e`: edit open note
- `r`: rename note
- `d`: delete note
- `/`: filter note titles
- `b`: show backlinks for the open note
- `s`: show/hide the notes sidebar
- `Esc`: leave editor/filter/backlinks
- `Ctrl-X`: save and exit editor
- `Ctrl-C` or `q`: quit

## Limits

The defaults are intentionally small:

- Maximum notes listed: 512
- Maximum note size loaded in editor/viewer: 48 KiB
- Maximum lines per note in editor: 2048
- Maximum line length: 240 bytes

These are compile-time constants near the top of `memex.c`.
