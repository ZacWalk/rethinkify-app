# AGENTS.md

dde is the editor for [dd](https://github.com/ZacWalk/dd) projects: a lightweight Windows editor with a folder browser, search, document views and C/C++ navigation, which runs dd's build, test, run and launch from its keys and menus and streams the output into a pane beside the code. See [README.md](README.md) for what it does and [docs/design.md](docs/design.md) for how it works — read the design document before making structural changes, and update it when you change high-level behaviour.

## Non-negotiables

1. **Windows-only code lives in the platform layer.** Everything in this repo talks to `pf::` abstractions declared in `platform.h`, which lives in the shared **libdesktop** repository along with its Win32 implementation. No `windows.h` types, Win32 API calls, `HWND`/`HDC`, or Win32 constants anywhere in this repo. A change that needs a new OS capability goes into libdesktop first, with its test in `tests/platform_tests.cpp` — then push it and bump `GIT_TAG` in `CMakeLists.txt`, or CI and a fresh clone build against the old revision. The other apps build against the same header, so a signature change breaks them until they bump their own pin.
2. **Build with CMake + Ninja.** In PowerShell 7.4+, use `.\dd.ps1 build debug` (`build` alone builds both configurations). The pinned shared dd driver and `dd.psd1` map to the existing CMake presets; keep the vendored runtime unmodified. libdesktop is still owned by `CMakeLists.txt`, preferring a sibling `..\libdesktop` checkout when one exists — develop the two together there. Do not add other dependencies.
3. **Run the tests.** `.\dd.ps1 test --label unit` builds and runs the `/test` suite through CTest in both configurations without a GUI; unfiltered `test` also runs desktop smoke tests. The unit runner is `exe\dde-64d.exe /test` — exits 0 on success, 1 on any failure. Add a test in `tests.cpp` for every behaviour you fix, and run libdesktop's own suite after touching it.
4. **Optimise for small and fast.** This is the point of the project. Avoid per-keystroke or per-paint allocation, avoid O(document) work for a local edit, prefer `string_view` and reusable buffers. Delete dead code rather than leaving it.
5. **Temporary files go in `tmp/`.**

## Conventions

- Modern C++ in `snake_case`. Some older code uses Win32 Hungarian (`nActualItems`, `pBuf`, `dwCookie`) — convert it when you touch it, do not add more.
- Text coordinates are **UTF‑8 byte offsets**, never codepoints or columns. Use `pf::utf8_prev` / `pf::utf8_next` to step; never `++`/`--` a byte index over text that may be non-ASCII.
- Views never repaint directly. Raise an `invalid::*` bit and let `app_idle` coalesce the work.
- Off-thread work must operate on a snapshot taken on the UI thread. The worker must not touch a live `document` or `index_item`. A running dd command adds a reader thread per pipe, inside libdesktop; those own nothing but a byte buffer and hand every line to the UI thread through `pf::run_ui`.
- A `document` belongs to one view's event sink. The Output pane's document and view have their own, because routing their changes to the document pane would corrupt that pane's wrap cache.
- Comments explain *why*, in one line. Do not narrate what the next line does.

## Where things live

| Area | Files |
|---|---|
| Platform abstraction | the separate **libdesktop** repo: `platform.h` and `platform_win.cpp` (entry point, windowing, drawing, files, config, clipboard, spell check, async, child processes, path containment), shared with the other apps, plus **libdesktop-ui** (`pf::ui`): the widgets, the text buffer, the syntax highlighters and the document, markdown and editing views |
| Application | `app.h`, `app.cpp` (main window, panes, splitters, document index, search, session, dd integration, the Output pane), `app_state.h` (state and testable logic) |
| Commands | `commands.h`, `commands.cpp` (`command_def` and lookup), `app_commands.cpp` (the command table and menu builder) |
| Text model | `document.h`, `document.cpp` (a `pf::ui::text_buffer` that knows its path, encoding, line endings, load/save, JSON reformat, sort), `document_syntax.cpp` (which highlighter a document gets) |
| Document views | `view_base.h`, `view_text.h`, `view_doc.h`, `view_doc_edit.h`, `view_doc_readonly.h`, `view_doc_markdown.h`, `view_doc_csv.h` and `view_doc_hex.h` name the shared `pf::ui` views under this application's names; `view_output.h` (the Output pane and its line colouring) is this application's own. The shared views handle Ctrl+A/C/X/V themselves, for applications that have no menu behind them — here the accelerator table answers first, so nothing is handled twice |
| Panel views | `view_list.h` names the shared `pf::ui::list_view`; `view_list_files.h` and `view_list_search.h` are this application's own, and hang an `index_item` or a search hit off each row's `data` |
| dd integration | `dd_project.h`/`.cpp` (what a `dd.psd1` offers, the probe that reads it, and the one dd command line), `tool_runner.h`/`.cpp` (a child process at a time, with a queue), `tool_output.h`/`.cpp` (MSVC, clang, CMake, Ninja, CTest and PowerShell diagnostics) |
| C/C++ navigation | `cpp_lex.h`/`.cpp` (tokens), `cpp_index.h`/`.cpp` (declarations and the symbol table) |
| Widgets | `ui.h` (the `pf::ui` widgets under this application's names: `edit_box`, `caret_blinker`, `splitter`, `custom_scrollbar`) |
| Utilities | `util.h`/`util.cpp` (string ops, colour), `json.h`/`json.cpp` (JSON reader), `calc.h` (expression parser for Calculate Selection), `gitignore.h` (index filtering) |
| Tests | `test.h` (assertions and runner), `test_process.h` (fake processes), `tests.cpp`, `tests_cpp.cpp` (the C++ and tool-output suites) |
| Build | `CMakeLists.txt` (declares the app with `platform_add_app()` and registers its `/test` suite with CTest), `CMakePresets.json`, `dd.psd1` (project settings), `dd.ps1` / `.dd/` (unmodified shared driver, hashes in `docs/dd-upstream.json`), `pch.h`, `targetver.h` |

## Adding a command

Add one entry to the table in `app_state::make_commands` (`app_commands.cpp`): description, menu text, `command_id`, accelerator, optional enabled/checked predicates, and the lambda. That single entry drives the menu item, its enable/check state, the runtime accelerator and the generated About document. Do not introduce a parallel dispatch table, and do not also handle the key in a view — the accelerator table consumes it first, so the view branch would be dead code. A second binding for the same command goes in `accel_alt`. Every binding is unique, and `should keep every binding unique` holds the table to it.

The dd menu's items that come from `dd.psd1` — a target, a test label, a project command — are generated by `build_dd_menu` and take no key. Everything with a key is a table entry.

The shared `pf::ui` views are the one exception, and they are not a counterexample: `text_view` handles Ctrl+A/C/X/V because an application with no menu behind a view would otherwise have no way to copy. Those keys never reach a view here, for exactly the reason above. If a shared view appears to handle a command this application also binds, the accelerator wins and the view's branch is unreachable; do not "fix" that by removing the binding.

Global accelerators fire regardless of focus, so a command that acts on "the selection" must decide what the focused pane means — the editor, the file list, the Output pane, or an inline edit box.

## Command line

| | |
|---|---|
| `exe\dde-64d.exe /test` | Run unit tests to stdout, no GUI |
| `exe\dde-64d.exe /spell:<word>` | Spell-checker diagnostics for `<word>`, no GUI |
| `exe\dde-64d.exe /dd[:<folder>]` | Read a folder's `dd.psd1` the way the dd menu does, through a real `pwsh`, and print what dde makes of it; no GUI |

All accept `/x` and `--x`. None writes configuration. **Output is only captured when stdout is piped** (`| Out-String`); redirecting to a file yields nothing, because the console binding reopens stdout.

`/dd` exists because `pf::run_ui` callbacks never drain under `/test` — there is no message loop — so the real probe cannot be unit tested. Run it after changing discovery, the runner or the platform's child-process code. Unit tests must never start PowerShell: give `app_state::spawn` (or `tools::runner::spawn`) a fake, as `test_process.h` does, and answer questions through `app_state::ask_user`.

## dd integration

The platform layer only starts the process and moves bytes: `pf::spawn_child_process` creates the pipes, runs a reader thread per pipe, reassembles whole lines and delivers each one on the UI thread. It knows nothing of dd, diagnostics or the Output pane. Keep it that way.

Above it, `tools::runner` runs one command at a time and queues the rest, `tools::output_parser` turns its lines into diagnostics as they arrive, and `app_state` shows them in the Output pane. `dd_project` is the only place that knows dd's interface. Discovery reads `dd.psd1` as data, with `Import-PowerShellDataFile` run from the root; nothing runs the project's `dd.ps1` or `.dd` scripts because a folder was opened. Every dd command is `pwsh -NoProfile -NonInteractive -File <root>\dd.ps1 <verb> <args> --non-interactive`, an argument array with the root as its working directory — never an assembled shell string, never an execution-policy override.

## dd build system

Run the repository gate after changes here:

```pwsh
.\dd.ps1 test --label unit
```

The filtered gate builds both configurations and runs the CTest tests carrying
the `unit` label, without dd's separate desktop smoke test.

dde vendors the [dd 0.2.5](https://github.com/ZacWalk/dd/releases/tag/v0.2.5)
build driver unmodified as `dd.ps1` and `.dd/`, with file hashes recorded in
[docs/dd-upstream.json](docs/dd-upstream.json). Project settings live in
`dd.psd1`; CMake owns the libdesktop dependency pin
(`dependencies.owner = 'application'`), currently the `GIT_TAG` in
`CMakeLists.txt`. Use `.\dd.ps1 commands --json` to inspect the dde command
manifest. dd's
[two modes](https://github.com/ZacWalk/dd/blob/main/docs/mcp.md#two-modes) —
CLI, and MCP for an MCP client — and its
[commands](https://github.com/ZacWalk/dd/blob/main/docs/cli.md#commands) are
documented in its own repository.