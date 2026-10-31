# dde — Design

How dde is put together and why. For what it does and how to build it, see [README.md](../README.md). For working-agreements when changing the code, see [AGENTS.md](../AGENTS.md).

## Goals

1. **Fast and small.** No dependencies beyond the Win32 API and the shared platform layer. Files load once into a single immutable buffer; lines are slices until edited. Typical memory use is a few megabytes.
2. **One obvious mental model.** The left pane lists things to open; the middle pane shows the current document; the Output pane, when it is showing, holds what dd printed last. Everything else is a mode of one of those.
3. **Nothing hidden.** No background writes, no autosave, no telemetry, and nothing runs because a folder was opened. Configuration is written at shutdown.
4. **dd is the build.** dde knows nothing of CMake or the compiler. It runs the project's dd, shows what dd prints, and turns the diagnostics in it into places to go.

## Layers

```
libdesktop (separate repo)      OS abstraction: windows, input, drawing, files, config, clipboard, spell check, async, child processes
        ↑
app.cpp / app_state.h           Application: window layout, document index, search, commands, session, dd
        ↑
view_*.h                        Panes: document views, list panels and the Output pane
document.* / document_syntax.*  Text model: lines, selection, undo, highlighting
dd_project.* / tool_*.*         dd: what dd.psd1 offers, the runner, diagnostics
cpp_lex.* / cpp_index.*         C/C++ index
json.* / util.* / calc.h        Leaf utilities
```

The platform layer lives in the shared [libdesktop](https://github.com/ZacWalk/libdesktop) repository, pulled in by CMake and used by the other apps in the workspace. `platform.h` declares OS-free types (`window_frame`, `frame_reactor`, `draw_context`, `file_path`, …); `platform_win.cpp` is the only implementation. Nothing in this repo includes `windows.h`, and no OS parameter reaches it raw: the messages that carry data (`timer`, `dpi_changed`, `drop_files`) arrive as a decoded `pf::message_params`.

### Build tooling

The unmodified [dd 0.2.5](https://github.com/ZacWalk/dd/releases/tag/v0.2.5)
runtime is vendored in `dd.ps1` and `.dd/`, with its upstream revision and file
hashes recorded in `docs/dd-upstream.json`. It is developer tooling, not an
application dependency. `dd.psd1` maps the Debug/Release presets and executable
paths; CMake continues to own the build graph and libdesktop acquisition. CTest
runs the executable's `/test` entry point, with the `unit` label letting CI skip
dd's separate desktop smoke tests.

### Threading

One UI thread and one worker thread. `pf::run_async` queues work onto the worker; `pf::run_ui` marshals results back, waking the message loop through `MsgWaitForMultipleObjects`. Only two operations run off the UI thread — **folder indexing** and **search** — and both take a snapshot of what they need on the UI thread first. The worker never dereferences a live `index_item` or `document`.

A running dd command adds two more threads, one per pipe, inside libdesktop's `pf::spawn_child_process`, because a blocking `ReadFile` cannot share the worker with indexing and search. They own nothing but a byte buffer: each splits its stream into lines and hands every complete line to the UI thread through `pf::run_ui`, guarded by a shared cancelled flag so no callback can arrive after the process object is gone.

Opening a document is also asynchronous, so anything that depends on the loaded text (such as selecting a search match) must be passed to `load_doc` as a completion callback. Tests use `deferred_scheduler`, which queues tasks and drains them on `pump()`, so they exercise the production ordering rather than completing inline.

### Invalidation

Views never repaint directly. They set bits in an atomic mask (`invalid::doc_layout`, `doc_caret`, `doc_scrollbar`, `files_layout`, `files_populate`, `search_layout`, `search_populate`, `output_layout`, `index`, `windows`, `app_title`). The message loop calls `app_idle()` once per pump, which coalesces layout, scrollbar recalculation, caret update, list population and repaint into one pass.

The document model distinguishes three notifications, and the difference is what keeps typing cheap:

- `invalidate_lines(start, end)` — **repaint only**. Raised by selection and caret movement. It touches no layout and no highlighting.
- `lines_changed(start, end)` — **the text changed**. Marks those lines dirty for word wrap, lowers the syntax-highlight cookie watermark, and raises `doc_layout`.
- `line_count_changed(at, delta)` — **lines were inserted after `at` or erased from `at + 1`**. The view splices its per-line arrays (wrap offsets, row prefix sum, highlight cookies) by `delta` instead of rebuilding them, so pressing Enter costs the same in a 50,000-line file as in a 5-line one.

`doc_view::layout()` then re-wraps only the dirty lines and patches the row prefix sum by the delta. A full re-wrap happens only when the document is replaced, the width changes, word wrap is toggled, or a splice cannot patch what it finds — either the cached arrays no longer describe the document, or an earlier edit left a dirty line range in the old numbering.

## Window layout

```
┌───────────────┬─┬───────────────────────────────┬─┬─────────────────┐
│ left pane     │ │ document pane                 │ │ Output pane     │
│ files  OR     │▓│ text / markdown / csv / hex   │▓│ > dd build debug│
│ search        │ │                               │ │ ...             │
│               │ │                               │ │ > exit code 0   │
└───────────────┴─┴───────────────────────────────┴─┴─────────────────┘
                 └ splitter (5px, DPI-scaled, ratio 0.05–0.95)
```

The Output pane is hidden until a dd command runs, and toggled with `Ctrl+Shift+U`. Its splitter divides only what is left of the document pane, so the two can never cross; with no room left it collapses against the right edge rather than leaving the window.

`view_mode` is the cross product of `view_content` (`edit_text`, `markdown`, `csv`, `hex`) and search-panel on/off. `app_state::set_mode` swaps the document pane's reactor for the matching view class and points the left pane at either the file list or the search panel.

## Document model

- A file is loaded once into an immutable `file_buffer` (capped at **2 MB**; larger files are truncated at a codepoint boundary and forced read-only). A truncated document refuses to save, so the rest of the file cannot be lost.
- `document` is a `std::vector<document_line>`. A line is either an owned `std::string` or a `(buffer, offset, length)` slice; slices cost 32 bytes and transcode UTF‑16 on render. Byte length is cached, so `size()` is O(1).
- All coordinates (`text_location.x`) are **UTF‑8 byte offsets**, not columns or codepoints. Display columns are derived through a per-line expanded-length cache that accounts for tabs.
- **Undo** is a linear `vector<undo_item>` with a redo cursor. Each item is an ordered list of insert/erase steps, replayed forward to redo and in reverse to undo. `undo_group` is an RAII scope that commits on destruction. Dirty state is `_undo_pos != _saved_undo_pos`, so undoing back to the last save marks the document clean.
- **Save** writes to a temp file and moves it into place. The detected encoding is preserved: a UTF‑16 file is written back as UTF‑16, and a BOM-less file never gains a BOM. Saving re-checks the on-disk timestamp and prompts if the file changed underneath.
- Documents live in the folder index. Switching files never prompts to save; unsaved documents stay in memory with their undo history and are shown in red. You are prompted only when changing the root folder or exiting. Unmodified documents past the **24** most recently opened are dropped and reloaded on demand — modified work and generated documents are never dropped.

### Syntax highlighting

Per-line, stateless except for a 32-bit carry cookie (in-comment / in-string flags) threaded from the previous line. Each highlighter resets and fills a sorted array of `text_block{char_pos, style}`. The view caches cookies with a "valid to line N" watermark and rescans backwards at most 1000 lines, so an edit invalidates highlighting in O(1). Languages: C++, Rust, Python, PowerShell, Markdown, hex, plain text.

The four C-like languages share one scanner driven by a `syntax_rules` table — line and block comment tokens, escape character, whether `#` starts a directive, and whether `-` may appear in an identifier. Adding a language of that shape is one table entry plus a keyword set.

## View hierarchy

```
pf::frame_reactor
└── view_base                  scroll offset and content extent, both in pixels
    ├── text_view              font metrics, screen lines, message bar, clipboard, zoom, Escape
    │   └── doc_view           document, caret, selection, word wrap, hit-testing, painting
    │       ├── edit_doc_view          writable: the document pane
    │       └── read_only_doc_view     no caret, no h-scroll, word wrap locked, keys scroll
    │           ├── markdown_doc_view
    │           ├── csv_doc_view
    │           ├── hex_doc_view
    │           └── output_view            the dd Output pane
    └── list_view              rows, selection, hover, keyboard navigation, row copy
        ├── file_list_view     folder tree, inline rename, drag-drop
        └── search_list_view   search box, grouped results
```

Everything above `output_view` and the two list panels is `pf::ui`, named locally by
a one-line header. A list row is a `pf::ui::list_item`; the panels hang their own
object off its `data` — an `index_item` for the folder browser, a search hit for the
search panel — and the shared list never looks inside it.

The split between `edit_doc_view` and `read_only_doc_view` is what makes the read-only panes predictable: they have no caret, cannot scroll horizontally, ignore Alt+Z, ignore Shift, and their arrow keys scroll rather than move an invisible cursor. Hex does not drag-select, because its layout is not the document's — `Ctrl+A` still selects everything so the text can be copied. Markdown hit-tests the layout it drew and selects like the editor; CSV selects whole records, since a cell is drawn padded into its column. Escape always returns them to the text editor.

## Panes and what each one does

| Pane | Purpose | Opens with | Leaves with |
|---|---|---|---|
| **Folder browser** | Navigate the root folder; create, rename and delete files | default | — |
| **Search** | Live text search across the root folder | `Ctrl+Shift+F` | `Escape` |
| **Text editor** | The only place text can be changed | default | — |
| **Markdown preview** | Read rendered `.md` | `Ctrl+M`, auto for `.md`/`.markdown` | `Escape` |
| **CSV table** | Read `.csv` as an aligned table | auto for `.csv` | `Escape` |
| **Hex** | Read binary files | auto for binary content | `Escape` |
| **Output** | What the last dd command printed, live | a dd command, `Ctrl+Shift+U` | `Escape` returns focus |

Each document remembers its own content view, so switching away and back restores what you were looking at.

### Folder browser

Click or arrow-key to preview a file (focus stays in the list); `Enter` opens it and moves focus to the editor. Clicking a folder expands or collapses it. Modified files are red; long names are ellipsized. Right-click gives New File, New Folder, Copy Path, Rename (`F2`) and Delete (Recycle Bin, with confirmation). A right-click inside a Shift-selected run keeps the run, so Copy Path takes every row in it; Rename and Delete act on one file, so a run offers neither. New items are named `new-file.md` / `new-folder`, suffixed `-2`, `-3`, … until unique. Names are validated against path separators, `.`/`..`, trailing dots and reserved device names. Files can be dropped onto the panel to copy them in.

### Folder index

Indexing is recursive and runs off the UI thread. `.git` is always skipped, and every `.gitignore` encountered on the way down is applied to the subtree below it — so build output stays out of the browser, out of search and out of memory. The matcher (`gitignore.h`) covers comments, `!` negation, directory-only `dir/`, anchored patterns and the `*`, `**` and `?` wildcards; character classes are not matched, so an unrecognised pattern leaves the file visible rather than hiding it silently.

### Search

Typing runs a live search, debounced by 150 ms; a newer query cancels the one in flight. Because a longer query can only match where a shorter one already did, a query that extends the previous one rescans only the files that previously matched — so each extra keystroke costs a handful of files rather than the whole folder. Any edit or reindex bumps a content generation that disables this narrowing until the next full scan.

Results are grouped under a collapsible per-file header showing the relative path and match count. Selecting a match opens the file and selects the match — the document may still be loading, so the selection is applied from the load continuation rather than immediately. Focus stays in the panel until `Enter`. `F8` / `Shift+F8` jump between matches. Limits: **5,000 results** (the count reads "limit reached" when hit), files over **10 MB** skipped, binary files skipped (extension list plus a content sniff of the first block, taken from the same read used to scan the file). A result keeps at most **400 bytes** of context around its match, so a minified file with megabyte-long lines cannot blow up either memory or paint time.

### Text editor

Character input with unlimited per-document undo. Word wrap (`Alt+Z`) is application state, applied to every editable view and persisted. Tab/Shift+Tab indent and unindent; `Ctrl+R` reformats JSON; Edit ▸ Sort & Remove Duplicates sorts case-sensitively and de-duplicates; `Ctrl+E` replaces a selected arithmetic expression with its value. Double-click selects a word, the left margin selects lines, `Ctrl+click` selects a word, `Ctrl+click` in the margin selects everything. Dragging a selection moves it, or copies it when `Ctrl` is held; `Escape` cancels the drag. The active row carries a subtle highlight band while the editor has focus.

### Markdown preview

Headings (H1–H3, size-scaled), bold and italic (rendered as colour, not weight), inline and fenced code, links, ordered and unordered lists with hanging indents, and tables with wrapped cells, width-capped columns and numeric right-alignment.

The view is `pf::ui::markdown_view`, shared with the other applications. It draws the **source** — markers and all — so what is selected is what is in the file, and it parses that source with `pf::ui::md` so it knows what each line means. Selection works as it does in the editor, including drag, double-click and the margin; a table row selects whole, because its cells are drawn padded into their columns. Clicking a link reports its target to the application, which decides what a target means; this app does not yet act on one.

### CSV table

RFC 4180 quoted fields: a comma inside quotes stays in its field, `""` shows as one quote, and a trailing comma ends the record with an empty field. A record is one line, though — a quoted field with a line break in it draws its second half as a new record (libdesktop README, *Known gaps*). Aligned pipe-delimited columns, a brighter header row followed by a separator, width-capped columns with wrapped cell text, and numeric right-alignment. Dragging selects whole records — a cell is drawn padded into its column, so there is no byte under the pointer to select — and copying gives the records as they are in the file.

### Hex

Offset (8 hex digits) | 16 bytes | ASCII.

## dd integration

dde builds nothing itself. It runs the project's own dd — the vendored `dd.ps1` in the root folder — and shows what dd prints. Nothing here is a linked dependency: `pwsh` and the toolchain dd drives are the user's own.

### Discovery is data only

When the root folder holds `dd.ps1`, the dd menu comes alive. When it also holds `dd.psd1`, dde reads it — as data:

```text
pwsh -NoProfile -NonInteractive -Command "Import-PowerShellDataFile -LiteralPath ./dd.psd1 | ConvertTo-Json -Depth 10 -Compress"
```

It runs with the root as its working directory, so no path is ever spliced into PowerShell source, and `Import-PowerShellDataFile` refuses anything that is not a literal: a manifest with code in it is reported as unreadable, not run. The project's `dd.ps1` and `.dd` scripts are never run because a folder was opened.

`dd::parse_manifest` is a pure function from that JSON to a `dd::project`: the project's name and default target; each target's id, kind, test label and whether it runs on this host; and each declared command's name, description, effects, dry-run support and parameters. `ConvertTo-Json` writes a one-element list as the element itself, so every list also accepts a single value. Names are held to dd's own rules, which also means a name can never be mistaken for an option on the command line. Anything that cannot be read leaves dd's own verbs — build, test, run, launch and the rest — rather than an error.

The probe has a runner of its own, so reading the manifest never waits behind a build. It runs when the folder is indexed, again only when `dd.psd1`'s timestamp has moved — after a refresh, or after saving it in dde — and on dd ▸ Refresh dd. A reply about a folder that is no longer open is dropped.

### One execution path

Every dd command is

```text
pwsh -NoProfile -NonInteractive -File <root>\dd.ps1 <verb> <args...> --non-interactive
```

run as an argument array with the root as its working directory — never an assembled shell string, never `-ExecutionPolicy`. `dd::command_line` is the only place that builds one. `tools::runner` runs one at a time; another request waits its turn in a bounded queue rather than being refused. A modified document prompts save / discard / cancel before anything runs, so a build never compiles stale text, and Clean and project commands that write confirm every time.

dd prints its own progress as `dd: ...` lines, streams every line its tools print to stderr unprefixed, and ends with a summary on stdout. Its exit code is 0 for success, 1 for a build or test failure, 2 for bad usage, 3 for a missing prerequisite, 4 for an environment that could not be set up and 5 for a dependency operation.

### The menu and its keys

| dd menu | Runs | Key |
|---|---|---|
| Build | `build <configuration>` | `F7`, `Ctrl+Shift+B` |
| Test | `test`, with `--label <label>` when one is chosen | `Ctrl+Shift+T` |
| Run | `run <target>`: the default target, else the only runnable one | `Ctrl+F5` |
| Launch | `launch <target>` | `Ctrl+Shift+F5` |
| Run Target ▸, Launch Target ▸ | the same for each runnable target, when there is more than one | |
| Configuration ▸ | Debug, Release or Both, for Build and Clean; remembered, Debug by default | |
| Test Label ▸ | All, or one of the targets' test labels | |
| Project Commands ▸ | each command `dd.psd1` declares | |
| Format | `fmt` | |
| Clean... | `clean <configuration> --yes`, after asking | |
| Doctor, Targets, Dependencies | `doctor`, `targets`, `dep list` | |
| Next / Previous Diagnostic | | `F8` / `Shift+F8` |
| Stop | the running command, and any waiting behind it | `Shift+F5` |
| Refresh dd | reads `dd.psd1` again | |

A target is runnable when it is not a library and its platforms, if it lists any, include `x64-windows`; Run and Launch are disabled for a project with nothing runnable. A project command marked `effects = 'read'` runs as it is. One that writes asks first and passes `--yes`, and when it supports `--dry-run`, No previews it instead. dde never asks for a parameter's value, so a command with one it cannot fill — no default, and either required or more than a flag that can stay off — is shown disabled with what it needs, `layout (needs --width, --dump, --file)`, to be run from a terminal. Generated items take no key, so no manifest can add a conflict.

Without `dd.ps1` every item is disabled and the menu's first line says so, as it does when `pwsh` is not on `PATH`, while `dd.psd1` is being read, and when it could not be.

### The Output pane

The right-hand pane is a read-only `output_view` over a document of its own. Starting a command clears it and opens the pane if it is hidden, without taking the keyboard. The first line is the command, `> dd build debug`; each line follows as it arrives; the last is a summary — exit code, elapsed time, errors and warnings, and any failed Ninja targets. The pane follows the newest line only while its reader is at the bottom, so scrolling up to read an error is not undone by the next line. Each line is coloured by what it is: the command and its summary, dd's own `dd: ` progress, an error or a warning.

The document and its view report to a sink of their own, so output can neither touch the editor's wrap cache nor repaint it. A line costs an append to the buffer and a splice of the view's per-line arrays — no document rebuild, and no layout at all while the pane is hidden. The pane keeps what the runner keeps: the first 2,000 lines, a `... N lines omitted ...` marker, and the newest 2,000, dropping from the middle 500 at a time so that a long build is not a memmove per line. The message bar shows the newest line, behind Ninja's `12/345` progress.

Double-click or `Enter` on a line that names a file opens it at that line and column, with the keyboard in the editor. A file the folder index leaves out — a fetched dependency, a sibling checkout — joins the folder browser rather than moving the root away from the project, and if it is still open at exit, the next session reopens it there too.

The pane belongs to the open project: opening another folder empties it and forgets its diagnostics, so `F8` cannot lead back into the project it replaced. A command still running when the folder changes keeps its output, and its diagnostics stay rooted where it ran.

### Diagnostics

`tool_output.cpp` turns the merged stream into diagnostics: MSVC, clang, the linker, CMake's indented error blocks, Ninja progress and `FAILED:` targets, CTest failures and PowerShell error records. ANSI escapes are stripped first. A `note:` attaches to the diagnostic above it, as does the indented block under it — MSVC's `with [ _Ty=int ]` template context belongs to the note it follows, not to the file. A line that matches nothing is kept verbatim; nothing is ever dropped.

Diagnostics are known as they arrive, so `F8` and `Shift+F8` work while the build is still running. They step through them in both directions, wrapping, select the Output line each came from, and open the file with the caret on the reported line and column. A diagnostic that names no line — a linker error naming an object file — reports in the message bar and moves nothing. While the search panel is active, `F8` steps through search results instead, as it always has. When a command finishes, an open document it rewrote — `fmt`, a project command — is reloaded if it has no unsaved changes.

## C++ navigation

dde parses C++ itself. `cpp_lex` produces a token stream — raw strings, line splices, directive continuations and all — and `cpp_index` walks it at declaration level, **skipping function bodies wholesale**, which is what makes it fast and what keeps it out of the hardest parts of the language. A symbol is name, scope, file, offset, line, column and kind; names and scopes are interned, and symbols are keyed by file so one file can be reparsed and spliced back. The whole of `src/` indexes at around 80 MB/s.

The index is rebuilt on the worker thread after a folder refresh, from paths snapshotted on the UI thread. Source files are decoded to UTF-8, including UTF-16 input, so index positions match document byte coordinates. A generation check rejects results from an older refresh or root folder. Only the UI thread reads the published index; resident source documents override disk text when it is published and before a lookup, including unsaved edits and undo back to saved text.

`F12` resolves the name at the caret, ranks the candidates — definitions before declarations, the current file before others, a type before a variable — and jumps to the best. Names resolve *lexically*, so an overload set is a set: pressing `F12` again on the same name offers the next candidate, and the message bar says which one of how many. That is the picker, without a popup. `Ctrl+F12` switches between a header and its source, preferring the sibling in the same folder. `Alt+Left` and `Alt+Right` walk two stacks of visited locations.

Right-clicking C/C++ text focuses the editor and offers **Go to Definition** and
**Switch Header/Source** alongside the usual editing commands. Definition lookup
uses the clicked name even inside a selection; Copy keeps the original selection
until a navigation command is chosen. Keyboard-opened context menus use the caret.
These commands are available only in the focused C/C++ text view, including
read-only source, not in previews, the file list, or the Output pane.

Candidate cycling retains the original file preference until the caret moves,
so jumping into another file cannot reorder the next result. Navigation messages
and selections are applied after asynchronous loading completes. Back/Forward
skip deleted entries, clamp obsolete positions to valid UTF-8 boundaries, and
reset when the root folder changes.

## Commands and keyboard

One `std::vector<command_def>` (`app_state::make_commands`) is the single source of truth for the menu bar, the enable/check state, the runtime accelerator table and the generated About document. Each entry owns its own lambda; adding a command is one table entry. Commands report through the message bar at the top of the document pane.

There is exactly one accelerator dispatcher: the Win32 accelerator table built from the menu, which routes to the command's lambda. Every binding lives in the table, including the secondary ones (`Ctrl+Ins`, `Shift+Ins`, `Shift+Del`, `Alt+Backspace`, `Ctrl+Shift+B`), which are declared as a command's `accel_alt`. Every binding is unique, and a test holds the table to it. A view only handles keys the table does not claim — caret movement, `Backspace`, `Tab`. Commands are focus-aware rather than focus-routed — each one asks what has focus and acts, instead of re-dispatching a synthetic keystroke to another window.

Two rules cover every command:

- **Clipboard and delete** (`Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Delete`, `Ctrl+A`) act on whatever has focus — the editor, the Output pane, the file list, or the search/rename edit box. An inline edit box wins for copy and cut only when it has a selection, so `Ctrl+C` in the search panel still copies the selected result's path. Switching to another application and back returns the keyboard to the pane that had it.
- **Text-editing commands** (undo, redo, reformat, sort, calculate, spell check) require the text editor: they are disabled while an inline edit box has focus, while a read-only preview is showing, and on a read-only document. `document::replace_text` also refuses on a read-only document, so no path can bypass the check.
- **Save and Save As** are disabled on a read-only document, and `document::save_to_file` refuses one outright, so a truncated file can never be written back over the original.

`F5` refreshes the focused panel: the search when the search panel has focus, the folder index otherwise. `F3` / `Shift+F3` step through search results and are disabled outside the search panel; `F8` / `Shift+F8` step through dd's diagnostics, or through the search results while the search panel is active, and neither opens the search panel, which is `Ctrl+Shift+F`. `Alt+Z` is disabled outside the editor.

Help ▸ About (`F1`) and Help ▸ Run Tests (`Ctrl+T`) generate a read-only, never-dirty document from the command table. They appear in the folder browser so you can return to them, but they are not files and never prompt to save.

| | |
|---|---|
| **File** | `Ctrl+N` new · `Ctrl+O` open · `Ctrl+S` save · `Ctrl+Shift+S` save all · File ▸ Save As · File ▸ Exit |
| **Edit** | `Ctrl+Z` undo (`Alt+Backspace`) · `Ctrl+Y` redo · `Ctrl+X`/`Ctrl+C`/`Ctrl+V` (also `Shift+Del`, `Ctrl+Ins`, `Shift+Ins`) · `Delete` · `Ctrl+A` · `Ctrl+R` reformat JSON · `Ctrl+E` calculate selection · `Ctrl+Shift+P` spell check · Edit ▸ Sort & Remove Duplicates |
| **View** | `Alt+Z` word wrap (editor only) · `Ctrl+M` markdown preview · `F5` refresh · `F3` / `Shift+F3` next/previous result (search only) · `Ctrl+Shift+F` search · `Ctrl+Shift+U` Output pane · `Ctrl++` / `Ctrl+-` / `Ctrl+Wheel` zoom |
| **dd** | `F7` build (also `Ctrl+Shift+B`) · `Ctrl+Shift+T` test · `Ctrl+F5` run · `Ctrl+Shift+F5` launch · `Shift+F5` stop · `F8` / `Shift+F8` next/previous diagnostic (search result while searching) |
| **Help** | `Ctrl+T` run tests · `F1` about |
| **Caret** | arrows · `Ctrl+←/→` word · `Home`/`End` · `Ctrl+Home`/`Ctrl+End` · `PageUp`/`PageDown` · `Ctrl+↑/↓` scroll only · add `Shift` to extend the selection |
| **Editing** | `Tab` / `Shift+Tab` indent · `Backspace` · `Ctrl+Backspace` delete word left |
| **Read-only views** | arrows, `Home`/`End`, `PageUp`/`PageDown` scroll · `Escape` returns to the editor |
| **Output** | `Enter` or double-click opens the file a line names · arrows, `PageUp`/`PageDown` scroll · `Escape` back to the editor |
| **Panels** | `↑`/`↓` navigate and preview · `Enter` open (or expand/collapse a folder or search group) · `F2` rename · `Delete` delete file · `Escape` close search |

Help ▸ About (`F1`) generates the authoritative shortcut list from the command table at runtime.

### Text input

`WM_CHAR` delivers UTF-16 code units. The platform layer combines surrogate pairs and hands the application a `char32_t`, which the editor and every inline edit box encode to UTF-8 before inserting. Word navigation classifies any non-ASCII byte as a word byte, so a scan can never stop between the lead and continuation bytes of one codepoint.

## Spell check

Three modes — `auto_detect` (default; active for `.md` and `.txt`), `enabled`, `disabled` — persisted in config and toggled with `Ctrl+Shift+P`. Misspellings are underlined in the editor; right-click offers suggestions and *Add to Dictionary*. The platform checker is created lazily and spell check is silently inactive when none is available.

## Configuration

An INI beside the executable when that folder is writable, otherwise `%LOCALAPPDATA%\dde\dde.ini`. Written at shutdown, so no command-line mode ever writes it.

| Section | Keys |
|---|---|
| `Window` | `Left`, `Top`, `Right`, `Bottom`, `Maximized` |
| `Font` | `TextSize`, `ListSize` (clamped 8–72) |
| `Splitter` | `PanelRatio` (clamped 0.05–0.95, default 0.2) |
| `Output` | `Visible`, `SplitterRatio` (0.05–0.95), `FontSize` (8–72) |
| `dd` | `Configuration` (`debug`, `release` or `both`) |
| `View` | `WordWrap`, `SpellCheck` |
| `Recent` | `Folder`, `Document` |
| `RecentFolders` | `Folder1`–`Folder8`, `Document1`–`Document8` |

Restored paths are validated: UNC and non-existent roots are skipped, and a restored document reopens inside its root even when the index leaves it out. Passing a file on the command line skips session restore entirely, and a file outside the current folder makes its own folder the root.

## Command line

| | |
|---|---|
| `dde-64d.exe /test` | Run the unit tests to stdout; exit 0 on success, 1 on any failure. No GUI. |
| `dde-64d.exe /spell:<word>` | Print spell-checker diagnostics and suggestions. No GUI. |
| `dde-64d.exe /dd[:<folder>]` | Read the folder's `dd.psd1` — the current directory's by default — through a real `pwsh`, as the dd menu does, and print the project, targets, test labels and commands dde found. Exit 1 when there was nothing to read. Runs no project script. No GUI. |
| `dde-64d.exe <path>` | Open a file. Arguments starting with `/` or `-` are ignored; the last plain argument wins. |

Both `/x` and `--x` forms are accepted.

## Known limitations

- Documents are capped at 2 MB; a larger file opens read-only showing the first 2 MB.
- CSV selects and copies whole records rather than individual cells, because a cell is drawn padded into its column; hex has no drag selection at all, though `Ctrl+A` copies it.
- A markdown table is drawn padded into its columns, so dragging over one selects whole source lines rather than individual cells.
- Case-insensitive search folds ASCII and Latin-1; other scripts compare case-sensitively.
- Saving a UTF-32 file writes UTF-8; UTF-8 and UTF-16 round-trip.
- Stop ends `pwsh`, not the CMake, Ninja and compiler processes dd started under it: `pf::child_process::terminate` ends only the process it started. Those run on to the end of their step, and the run is reported stopped once they exit and close its output.
- dde asks for no parameter values, so a project command that needs one is shown disabled; run it from a terminal.
- dd writes its output in `pwsh`'s console code page, which dde reads as UTF-8, so a non-ASCII character in a build line can arrive garbled.
