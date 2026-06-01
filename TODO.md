# memex TODO

Roadmap for closing the biggest Obsidian-style feature gaps while keeping
`memex` small, curses-based, and workable on constrained systems.

## v0.2

Focus: complete the core note-reading and note-finding workflow.

- [x] Full-text search across note contents.
  - [x] Show results in a dedicated list with short context snippets.
  - [x] Support jumping directly from a result into the target note.
- [x] Improve read mode rendering.
  - [x] Add soft wrapping for paragraphs.
  - [x] Render nested bullet lists and nested checklists more clearly.
  - [x] Distinguish inline code from normal text.
  - [x] Improve heading spacing and section separation.
- [x] Add outline navigation from markdown headings.
  - [x] Build a per-note heading index when loading a note.
  - [x] Let users jump to a heading from a small outline pane or list.
- [x] Expand wiki-link handling.
  - [x] Support aliased links like `[[Note|Label]]`.
  - [x] Support heading links like `[[Note#Heading]]`.
  - [x] Replace number-only link following with cursor or selection-based navigation.
- [x] Make rename update inbound wiki links.
  - [x] Scan notes for `[[Old Title]]` references and rewrite them safely.
  - [x] Keep the existing file rename behavior.
- [x] Replace hard delete with trash/archive behavior.
  - [x] Move deleted notes into a configurable trash folder inside the note root.
- [x] Persist lightweight UI state.
  - [x] Restore last-opened note.
  - [x] Restore sidebar visibility.
  - [x] Restore preferred read/write mode.

## v0.3

Focus: improve organization and editing ergonomics.

- [x] Add tag support.
  - [x] Parse inline tags like `#tag`.
  - [x] Add a tag browser or tag filter view.
- [x] Add YAML frontmatter support.
  - [x] Parse frontmatter without requiring a full markdown parser.
  - [x] Expose common fields such as title, tags, and aliases.
- [x] Add note sorting modes.
  - [x] Alphabetical.
  - [x] Modified time.
  - [x] Created time if available.
- [x] Replace the flat note list with a folder tree sidebar.
  - [x] Support nested directories.
  - [x] Support expand/collapse in the tree.
- [x] Add templates for new notes.
  - [x] Allow a default note template.
  - [x] Add optional specialized templates, such as daily note templates.
- [x] Add daily notes.
  - [x] Open or create today’s note.
  - [x] Support configurable date-based filenames.
- [x] Improve the editor workflow.
  - [x] Continue lists and checklists on newline.
  - [x] Toggle checkbox state on the current line.
  - [x] Add find within the current note.
  - [x] Add undo/redo.
  - [x] Warn before quitting with unsaved changes.
- [x] Add note navigation history.
  - [x] Back/forward through recently opened notes.
- [x] Add recent notes and pinned notes.

## v0.4

Focus: turn `memex` into a more complete knowledge-base tool.

- [x] Add indexed backlinks and indexed full-text search.
  - Avoid rescanning all notes for common queries.
- [x] Add unlinked mentions.
  - Find note titles referenced in plain text.
- [x] Add link autocomplete while editing.
  - Offer note-title completion for `[[...]]`.
- [x] Add transclusions.
  - Support embeds like `![[Note]]`.
- [x] Improve markdown coverage further.
  - [x] Tables.
  - [x] Better fenced code block rendering.
  - [x] Better blockquote and callout presentation.
- [x] Add a command palette for common actions.
- [x] Add saved searches.
- [x] Add a configuration file.
  - [x] Keybindings.
  - [x] Theme options.
  - [x] Default mode and startup behavior.
  - [x] Trash and template paths.
- [x] Add richer metadata views.
  - [x] Backlinks pane.
  - [x] Tags pane.
  - [x] Note info pane.
- [ ] Evaluate whether a graph view fits the project.
  - This is optional and may conflict with the small-TUI design goal.

## Recommended Build Order

1. [x] Full-text search
2. [x] Better read mode rendering with wrapping
3. [x] Heading outline and section jump
4. [x] Rename with wiki-link rewrite
5. [x] Tag support
6. [x] UI state persistence
7. [x] Folder tree sidebar
8. [x] Link autocomplete
9. [x] Indexed search and backlinks

## Minimal High-Value Target

If `memex` stays intentionally small, these are the highest-value features to
finish first:

- [x] Full-text search
- [x] Better read mode rendering
- [x] Outline view
- [x] Rename with link updates
- [x] Tags
- [x] UI state persistence
