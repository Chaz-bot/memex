# memex TODO

Roadmap for closing the biggest Obsidian-style feature gaps while keeping
`memex` small, curses-based, and workable on constrained systems.

## v0.2

Focus: complete the core note-reading and note-finding workflow.

- [ ] Full-text search across note contents.
  - Show results in a dedicated list with short context snippets.
  - Support jumping directly from a result into the target note.
- [ ] Improve read mode rendering.
  - Add soft wrapping for paragraphs.
  - Render nested bullet lists and nested checklists more clearly.
  - Distinguish inline code from normal text.
  - Improve heading spacing and section separation.
- [ ] Add outline navigation from markdown headings.
  - Build a per-note heading index when loading a note.
  - Let users jump to a heading from a small outline pane or list.
- [ ] Expand wiki-link handling.
  - Support aliased links like `[[Note|Label]]`.
  - Support heading links like `[[Note#Heading]]`.
  - Replace number-only link following with cursor or selection-based navigation.
- [ ] Make rename update inbound wiki links.
  - Scan notes for `[[Old Title]]` references and rewrite them safely.
  - Keep the existing file rename behavior.
- [ ] Replace hard delete with trash/archive behavior.
  - Move deleted notes into a configurable trash folder inside the note root.
- [ ] Persist lightweight UI state.
  - Restore last-opened note.
  - Restore sidebar visibility.
  - Restore preferred read/write mode.

## v0.3

Focus: improve organization and editing ergonomics.

- [ ] Add tag support.
  - Parse inline tags like `#tag`.
  - Add a tag browser or tag filter view.
- [ ] Add YAML frontmatter support.
  - Parse frontmatter without requiring a full markdown parser.
  - Expose common fields such as title, tags, and aliases.
- [ ] Add note sorting modes.
  - Alphabetical.
  - Modified time.
  - Created time if available.
- [ ] Replace the flat note list with a folder tree sidebar.
  - Support nested directories.
  - Support expand/collapse in the tree.
- [ ] Add templates for new notes.
  - Allow a default note template.
  - Add optional specialized templates, such as daily note templates.
- [ ] Add daily notes.
  - Open or create today’s note.
  - Support configurable date-based filenames.
- [ ] Improve the editor workflow.
  - Continue lists and checklists on newline.
  - Toggle checkbox state on the current line.
  - Add find within the current note.
  - Add undo/redo.
  - Warn before quitting with unsaved changes.
- [ ] Add note navigation history.
  - Back/forward through recently opened notes.
- [ ] Add recent notes and pinned notes.

## v1.0

Focus: turn `memex` into a more complete knowledge-base tool.

- [ ] Add indexed backlinks and indexed full-text search.
  - Avoid rescanning all notes for common queries.
- [ ] Add unlinked mentions.
  - Find note titles referenced in plain text.
- [ ] Add link autocomplete while editing.
  - Offer note-title completion for `[[...]]`.
- [ ] Add transclusions.
  - Support embeds like `![[Note]]`.
- [ ] Improve markdown coverage further.
  - Tables.
  - Better fenced code block rendering.
  - Better blockquote and callout presentation.
- [ ] Add a command palette for common actions.
- [ ] Add saved searches.
- [ ] Add a configuration file.
  - Keybindings.
  - Theme options.
  - Default mode and startup behavior.
  - Trash and template paths.
- [ ] Add richer metadata views.
  - Backlinks pane.
  - Tags pane.
  - Note info pane.
- [ ] Evaluate whether a graph view fits the project.
  - This is optional and may conflict with the small-TUI design goal.

## Recommended Build Order

1. [ ] Full-text search
2. [ ] Better read mode rendering with wrapping
3. [ ] Heading outline and section jump
4. [ ] Rename with wiki-link rewrite
5. [ ] Tag support
6. [ ] UI state persistence
7. [ ] Folder tree sidebar
8. [ ] Link autocomplete
9. [ ] Indexed search and backlinks

## Minimal High-Value Target

If `memex` stays intentionally small, these are the highest-value features to
finish first:

- [ ] Full-text search
- [ ] Better read mode rendering
- [ ] Outline view
- [ ] Rename with link updates
- [ ] Tags
- [ ] UI state persistence
