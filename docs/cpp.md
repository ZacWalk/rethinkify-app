# C++ tools

This document describes the planned C++ project and code tools for Rethinkify:
the small set of operations used every day, built without adding a dependency
to the editor and without turning it into a build system.

The current editor implements lexical Go to Definition (`F12`), Switch
Header/Source (`Ctrl+F12`), and Back/Forward (`Alt+Left` / `Alt+Right`).
Find References, symbol pickers and refactoring commands below remain planned;
their listed bindings do not mean those commands are available yet.
See [design.md](design.md#c-navigation) for the implemented navigation behavior.

## Decisions

1. **Nothing is linked or shipped but Rethinkify.** All semantic understanding
   of C++ comes from Rethinkify's own lexer and declaration parser, running
   in-process. No clangd, no Language Server Protocol, no libclang, no
   tree-sitter, no regular-expression engine.
2. **All understanding of tool results comes from in-process parsers** over a
   child process's output stream. Compiler, CMake, Ninja and CTest output is
   turned into structured diagnostics here.
3. **External programs are run, never linked.** PowerShell, CMake and the
   compiler are the user's toolchain, invoked as child processes through
   `pf::spawn_child_process`. Every one of them is optional.
4. **CMake is a source of facts, not a requirement.** When a build directory
   exists, its
   [File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
   reply supplies targets and include directories, parsed with the existing JSON
   DOM. With no CMake, the index works from the file tree alone.
5. **Nothing runs because a folder was opened** except the index, which reads
   bytes the folder indexer is already reading. Configuring, building, running
   and rewriting files all require an explicit command.
6. **Every operation is a function returning a value.** The menu, the command
   line and the MCP server are three front ends over one service. There is no
   second dispatcher and no operation that exists only as a menu handler.

## What this can and cannot do

The index resolves names *lexically*, with scope heuristics. It is not a
compiler front end and will never be one. State the consequences plainly,
because every later bug report is one of them:

- overloads resolve to a **set**, not to a single symbol;
- template-dependent names — anything reached through `T::`, or through a
  dependent base — are unresolvable;
- argument-dependent lookup, `using` declarations and namespace aliases are
  approximated by scope proximity;
- macros that construct declarations produce no symbol unless configured;
- code generated during the build is invisible until it exists on disk.

What is gained is not a consolation prize:

- it indexes code that does not compile, does not configure, and is half-typed;
- there is no server to start, so the first jump is instant;
- the index costs megabytes, not gigabytes, and no separate process;
- it reads the same bytes the folder index already reads, on the same pass;
- diagnostics come from the project's real compiler, so they are exactly the
  errors the build produces rather than a second opinion.

Where a result is uncertain the UI says so and offers the candidates. A picker
listing three declarations of `size()` is more useful than a confident jump to
the wrong one.

## Workspace model

Rethinkify has one open root folder, so that folder is the workspace. Opening a
different root replaces the model and stops any project-specific process.

The application owns one `cpp_workspace`:

- the root `dd.ps1`, its commands and its option sets;
- CMake presets, configured build directories, targets and include directories;
- the active configure preset, configuration and target;
- the symbol index and the file table it is keyed by;
- the last tool run and its parsed diagnostics.

Discovery and indexing run off the UI thread and publish immutable snapshots
through `pf::run_ui`. They must not retain pointers to live `document` or
`index_item` objects.

## Menus

Add **Tools** between **View** and **Help**, rebuilt when the root folder or
project model changes:

```
Tools
  dd.ps1
    run
    build
    test
    clean
    Configuration
      Debug
      Release
  CMake
    Configure
    Build
    Build Target
    Run Tests
    Refresh Project Model
  Check Current File
  Stop Running Tool
```

Only applicable items appear. `dd.ps1` command and option names come from the
script; preset and target names come from CMake. Dynamic items have no
accelerators initially.

Static and dynamic commands both flow through the single command/menu system.
Reserve a menu-id range for ephemeral workspace commands, rebuild that part of
the command table with the menu, and let the generated About document include
any dynamic command that later gains a key binding.

| Navigate | Binding |
|---|---|
| Go to Definition | `F12` |
| Find References | `Shift+F12` |
| Go to Symbol in Workspace | `Ctrl+T` |
| Go to Symbol in File | `Ctrl+Shift+O` |
| Switch Header/Source | `Ctrl+F12` |
| Next / Previous Diagnostic | `F8` / `Shift+F8` |
| Back / Forward | `Alt+Left` / `Alt+Right` |

| Refactor | Binding |
|---|---|
| Rename Symbol | `F2` while the editor has focus |
| Extract Variable | none |
| Extract Lambda | none |

`F2` remains file rename while the file list has focus. That is one
focus-aware command, not two competing key handlers.

## Tool runner

`tool_runner` is the common execution path for `dd.ps1`, CMake, CTest and the
compiler. A command is an executable, an argument array, a working directory and
a display name. Arguments go straight to `pf::spawn_child_process`; no shell
command string is assembled.

A run produces a value, not a side effect:

```
tool_result { command, working_dir, exit_code, elapsed, output, diagnostics, truncated }
```

The generated read-only `tool-output.md` document is one *rendering* of that
value. The error list is another. The MCP reply is a third. Nothing may exist
only inside the document.

Runs are queued, not refused. Each queue entry records who asked — the user or
an MCP client — so a build requested by an agent while the user's build is
running waits its turn instead of silently doing nothing. The queue is bounded;
a full queue is an error the caller can see. Cancellation applies to a queue
entry by id, whether it has started or not.

While a run is in flight:

- its menu command is disabled and **Stop Running Tool** is enabled;
- stdout and stderr are collected in arrival order, with stderr identified;
- the message bar shows the command and the newest output line;
- output is bounded in memory, keeping the beginning and the end;
- diagnostics are parsed incrementally, so the error list fills as it goes.

Stopping must terminate the process tree, not just the immediate PowerShell or
CMake process. If `pf::child_process` cannot do that, the capability and its
test go into **platform-h** first, then this repository's pin moves. No process
handles or job-object details belong here.

Modified files are not saved automatically. If any document is dirty, offer
**Save All and Run**, **Run Without Saving** and **Cancel**. The chosen command
runs with the workspace root as its working directory.

### `dd.ps1` discovery

Look for `<root>/dd.ps1`. The supported convention is a first positional string
parameter — conventionally `Command` — carrying a literal `ValidateSet`, plus
optional named parameters with literal `ValidateSet` values such as
`-Config Debug,Release`. A legacy script with these declarations yields `run`,
`build`, `test` and `clean` plus a `Debug`/`Release` choice.

Do not execute a script to discover its interface. Invoke PowerShell with a
small command that calls
`System.Management.Automation.Language.Parser.ParseFile`, walks the parameter
AST and prints JSON. That uses PowerShell's own grammar without adding a
PowerShell parser here. Prefer `pwsh.exe`, fall back to `powershell.exe`, and
run the selected command with the same executable.

If there is no supported command parameter, show a single **Run dd.ps1**.
Dynamic parameters and command names computed by running script code are
deliberately not discovered.

A selected item for such a legacy script runs an argument array:

```
pwsh.exe -NoProfile -NonInteractive -File <root>/dd.ps1 build -Config Debug
```

Do not add `-ExecutionPolicy Bypass`; the editor respects the user's policy.
Destructive commands such as `clean` are never invoked implicitly and always
confirm.

This repository's build now uses the unmodified
[dd v0.1.0](https://github.com/ZacWalk/dd/releases/tag/v0.1.0) shared driver,
with project settings in `dd.psd1`. It has no `ValidateSet` parameter block,
so current discovery offers only **Run dd.ps1**, which prints help. Use
`.\dd.ps1 build debug` and `.\dd.ps1 test --label unit` in PowerShell 7.4+
or the VS Code tasks. Supporting this driver's manifest and command interface
in the editor is separate future work; discovery must still not execute
project scripts merely because a folder was opened.

## Output parsers

One `tool_output.cpp`, table-driven, hand-written matchers over `string_view`.
No `std::regex` — it is slow, heavy and unnecessary for line shapes this
regular. Each parser is a small function returning an optional diagnostic.

Formats to recognise:

- **MSVC** — `path(line,col): error C2065: text`, `path(line): warning C4996:`,
  `fatal error C1083`, and linker `LNK2019` with no file or line. The
  `see declaration of 'x'` and `while compiling class template member function`
  lines are continuations belonging to the diagnostic above them.
- **clang / gcc** — `path:line:col: severity: text`, `note:` continuations, and
  `In file included from …` chains.
- **Ninja** — the `[123/456]` progress prefix drives the message bar; `FAILED:`
  marks where a failing command's output begins.
- **CMake** — `CMake Error at path:line (command):` followed by an indented
  block. `-- ` status lines are not diagnostics.
- **CTest** — `Test #12: name .... Passed 0.03 sec`, the
  `The following tests FAILED:` summary, and the `--output-on-failure` body.
- **PowerShell** — multi-line error records.

Two invariants. A diagnostic owns its continuation lines as one record. A line
matching nothing is retained verbatim — **never drop a line**.

Strip ANSI CSI escape sequences before matching, and pass `NO_COLOR=1` in the
child environment: `pwsh` and modern compilers colour their output even when it
is redirected. Doing only one of the two leaves escape bytes in file paths.

`dd.ps1` emits whatever the tools beneath it print, so the parsers run over the
merged stream regardless of which command produced it. There is one parser
stack, not one per command.

This is pure string-to-struct code, so its test coverage should be close to
total and costs nothing: every format above gets fixture lines in `tests.cpp`.

## Diagnostics

Errors come from the compiler the project already uses, at two scales.

**Whole project** — parsed from the build output. These are ground truth.

**One file, on demand** — a syntax-only compile of the current translation unit
(`cl /Zs`, or `-fsyntax-only` for a clang-like driver) using that file's flags
from `compile_commands.json`. Sub-second for one file. Bound to an explicit
**Check Current File** command; never automatic, never on save, never on a
timer.

Both feed one diagnostic list. The generated `tool-output.md` document *is* that
list to read — grouped, with each diagnostic's code, location and notes — and
`F8` / `Shift+F8` are how you work through it, wrapping in both directions and
putting the caret on the reported line and column. A diagnostic keeps its
compiler code (`C2065`), because that is what the user searches for and what an
agent can reason about.

A dedicated left-hand panel is deliberately not the first version. It would make
the left pane a three-way choice rather than files-or-search, which is a change
to `view_mode` worth making only once the list has earned it. A diagnostic that
names no line — a linker error naming an object file — reports in the message
bar and moves nothing.

## CMake project model

### Why `CMakeLists.txt` is not parsed

The CMake language has variables, functions, includes, conditionals,
toolchain-dependent branches, generator expressions and arbitrary process
execution. A text parser cannot determine the targets or the flags. The File API
exists to expose CMake's evaluated model as versioned JSON.

Rethinkify writes a client-owned query below the selected build directory:

```
.cmake/api/v1/query/client-rethinkify/query.json
```

requesting `codemodel` v2 (projects, directories, targets, sources, compile
groups, includes, defines, artifacts), `cmakeFiles` v1, `toolchains` v1 and
`cache` v2. CMake reads the query during configure and writes replies under
`.cmake/api/v1/reply`. Always start from the newest reply index and follow its
`jsonFile` references; reply filenames are opaque. If a referenced file
disappears because CMake wrote a new reply concurrently, restart from the new
index.

### Presets

Read `CMakePresets.json` and `CMakeUserPresets.json` with the existing JSON
parser. Resolve `include` files, preset inheritance, `condition` objects, and
the `${...}`, `$env{...}` and `$penv{...}` macros needed to find `binaryDir`.
Hidden and condition-disabled presets supply inherited values but do not appear
in the menu. Invoke CMake by preset name rather than reproducing presets as
flags:

```
cmake --preset debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset debug
ctest --preset debug
```

A **build** preset is not a configure preset: `cmake --build --preset debug`
fails when only a configure preset of that name exists, so fall back to
`cmake --build <binaryDir>`. Likewise use `ctest --test-dir <build-dir>` when no
test preset matches, adding `-C <configuration>` for a multi-configuration
generator.

Only pass `CMAKE_EXPORT_COMPILE_COMMANDS` when the generator supports it.
Preserve a project value that explicitly disables it, and report that the index
will fall back to file-relative include resolution.

With no presets, use an existing build directory whose `CMakeCache.txt` names
the workspace as `CMAKE_HOME_DIRECTORY`. Otherwise offer an explicit configure
command defaulting to `<root>/build`.

Reading existing File API replies during folder discovery is safe. Running CMake
is not — CMake files execute processes — so configure happens only on an
explicit command.

### What the model is for

Building and testing, target selection, and **include directories for the
index**. Later UI may group the file list by target, but the first version
leaves the physical folder tree unchanged: a source can belong to several
targets, so target membership must not become file ownership.

## The C++ index

Three pieces, in order of size.

### Lexer

A real C++ token scanner over a whole buffer: identifiers (bytes ≥ 0x80 are
identifier bytes), preprocessing numbers including digit separators, character
literals, string literals with every prefix, raw strings `R"tag(...)tag"`,
line and block comments, and punctuation. It tracks line numbers, marks the
first token on each line, and marks tokens inside a preprocessor directive —
including its backslash continuations — because the parser skips them wholesale.

A `//` comment continues onto the next line when the line ends in a backslash.
That is real C++ and a classic source of mis-scanning; it is a test case.

The per-line cookie scanner in `document_syntax.cpp` is **not** reused and must
not be. Its O(1) per-line invalidation is what keeps typing cheap in a large
file, and it cannot represent the multi-line constructs above. Two scanners, one
token vocabulary, and one line in the code saying why.

Tokens are slices of the buffer — offset and length, so no text is copied and a
token cannot span a line splice. An identifier broken across lines by a
backslash is therefore not recognised; that is an accepted limitation.

### Declaration parser

A brace, paren and bracket matcher with a scope stack: `namespace` →
`class` / `struct` / `union` / `enum class` → function. It emits a symbol for
each declaration at scope level and then **skips function bodies wholesale**,
recording only their byte range. No syntax tree, no type analysis, one small
stack, one pass, O(bytes).

That is the whole trick: almost all of C++'s difficulty lives inside function
bodies, and almost no navigation target does.

### Symbol table

```
symbol { name_ref u32, scope_ref u32, file_ref u32, offset u32, line u32, kind u8, flags u8 }
```

Twenty-four bytes, names interned in one table whose entries never move, keyed
by file so a reparse splices one file's range out and reinserts it. A
single-file reparse is fast enough on a debounced idle that incremental parsing
*within* a file is unnecessary; do not build it.

The qualifier belongs to the name directly after `::`, and to nothing else:
`std::vector<T> tokenize(...)` declares `cpp::tokenize`, not `cpp::std::tokenize`.
That one rule is what keeps out-of-line definitions (`void app_state::open()`)
and qualified return types apart.

### The preprocessor

**Index every branch of every `#if`**, treat macros as opaque, and record the
conditional nesting each symbol was found under. Evaluating conditionals
correctly needs include resolution and macro expansion — that is a compiler.
Indexing all branches is what tag-based tools have always done, and it is right
for navigation: a declaration in the branch you are not building is still a
declaration you want to reach. Mark such results rather than hiding them.

Keep one global macro table — name to replacement text — for exactly two
purposes: knowing that `FOO(x)` is a macro invocation and not a call, and
expanding a short user-configured list of declaration macros. Nothing else.

`#include "x.h"` resolves against the including file's directory, then the
CMake model's include directories when present, then the root.

### References are not stored

Storing every identifier occurrence would cost tens of megabytes and dominate
the process. Find References is instead the **existing folder search** — already
off the UI thread, already `.gitignore`-filtered — with two additions: the lexer
rejects hits inside comments, strings and disabled-looking regions, and each
surviving hit is scored against the symbol's scope. Zero index memory, and it
reuses the machinery in `app.cpp` instead of growing a second copy.

If that proves too slow on a large tree, add a per-file bitset of name hashes to
skip files that cannot contain the name. Measure before building it.

## Navigation

At the caret, resolve the identifier against the symbol table, ranked by scope
proximity: definitions before declarations, the current file before others, a
type before a variable. One confident result jumps; several are **cycled** —
pressing the key again on the same name offers the next, with the message bar
saying which one of how many. That is the picker the plan called for, minus the
popup: the platform layer has `screen_to_client` but no `client_to_screen`, so
placing a menu at the caret would mean a change in another repository for a
mouse-only affordance. Revisit it when something else needs that call.

Locations are canonicalised before opening. Workspace files open through the
normal folder-index and document path; files outside the root open read-only.

Every successful jump pushes the current path and caret onto a back stack and
clears the forward stack; `Alt+Left` and `Alt+Right` walk the two. Applying
history uses `load_doc`'s completion callback, because the destination may still
be loading.

Find References populates the search result view from located hits rather than a
text query, grouped **confident** and **possible**, and navigating one uses the
same asynchronous selection path as ordinary search.

## Refactoring

### Rename

Rename is **preview-first, always**. Resolve the symbol, collect every candidate
site, group by confidence with the confident group checked, and apply only what
the user confirms. This is the honest design for a lexical index — and it is
arguably better than a silent partial rename, because the sites it is unsure
about are exactly the ones worth looking at.

Before changing anything, validate the whole edit:

- every target is a regular text file inside the workspace;
- no target is truncated by the 2 MB cap or read-only — either aborts the whole
  rename;
- affected documents are pinned so the document cache cannot evict one
  mid-transaction;
- ranges are valid, non-overlapping and on UTF-8 boundaries.

Load every affected document first, then apply edits from the end of each file
inside one `undo_group` per document. All-or-nothing; never saves. A failed
preflight leaves every file untouched. Successful edits stay dirty in memory and
show red in the file list.

### Extract Variable

`auto name = <expr>;` needs no type information at all. Validate that the
selection is brace, paren and bracket balanced, contains no `;` or `return`, and
lies inside a function body the parser recorded. Insert the declaration before
the statement containing the selection and replace the selection with the name.

### Extract Lambda

A full extract-to-function needs parameter types, which this index does not
have. `auto name = [&] { ... };` placed immediately before the selection
captures everything by reference and writes no types. Ship that; promoting the
lambda to a free function stays manual, and the documentation says so rather
than pretending otherwise.

## MCP server

The same operations must be callable by an agent, so `cpp_service` gets a third
front end. Rethinkify is the MCP **server**.

**Connection.** The hosted agent already receives an `mcpServers` array in
`session/new`, currently empty. Advertise `command = <own exe>`,
`args = ["/mcp:<token>"]`; that child relays newline JSON between its stdio and
a per-session named pipe back to the editor. The token authorises the
connection and the pipe is user-scoped, so no socket is opened. The duplex pipe
API belongs in **platform-h**, with its test, before anything here uses it.

**Shared plumbing.** MCP is JSON-RPC 2.0 over newline-delimited JSON — the same
framing `acp::client` already uses. Factor request-id allocation, the pending
reply table, the bounded-pending limits and the error codes out of `acp.cpp`
into a `jsonrpc` core, and let ACP and MCP be two thin dialects over it.

**Tools**, split by effect, because that determines the permission model:

| Read-only, no prompt | Mutating or executing, prompted |
|---|---|
| `list_project`, `search_symbols`, `find_definition`, `find_references`, `document_outline`, `get_diagnostics`, `get_open_documents`, `get_selection`, `read_document` | `rename_symbol`, `apply_edit`, `write_document`, `run_dd_command`, `cmake_configure`, `cmake_build`, `run_tests`, `check_file` |

Query tools return **candidates with a confidence field**. That suits an agent
better than it suits a jump-to-definition UI: it can read all three candidates.
`get_diagnostics` carries the compiler's own error code.

`rename_symbol` returns the *preview* and a preview id; `apply_edit` commits it.
The agent gets no capability the user does not have.

**The security rule that matters most:** no tool takes a free-form command line,
executable path or working directory. An agent acting on text it read from a
file is a prompt-injection path straight to code execution. Every argument is
constrained to a value the editor discovered — a `ValidateSet` member, a target
from the CMake model, a path inside the root checked with `pf::is_path_within`.
Mutating and executing tools go through the existing `agent_host` permission
prompt. `clean` confirms every time regardless of any allow-list.

Because the index is in-process, an MCP call is one hop: reader thread →
`pf::run_ui` → query → reply. There is no foreign-process round trip to time
out. A call that cannot be answered still gets a JSON-RPC error reply; no path
may drop one silently, or the agent waits forever.

**Trust.** Configuring with CMake or running the compiler executes code from the
opened folder — `CMakeLists.txt`, a toolchain file, a preset. Record a trust
decision per root folder and require it before any tool runs, and require it
again before an MCP client can run one.

**Resources.** Expose the last tool output and the current diagnostics as MCP
resources (`rethinkify://tool-output/<id>`, `rethinkify://diagnostics`), so a
truncated tool reply can be pulled in full deliberately rather than flooding the
agent's context by default.

**Testing.** An in-process fake MCP client drives `mcp_server` directly in
`tests.cpp` for every tool and every rejection. A `/mcp` command-line mode
alongside `/acp` and `/agent` covers the real handshake, because `pf::run_ui`
callbacks never drain under `/test`.

## Failure handling

Missing tools are ordinary feature states:

- no PowerShell — `dd.ps1` items explain it was not found;
- no CMake — CMake items are disabled, `dd.ps1` and the index still work;
- no File API reply — request configure rather than guessing the project;
- no compilation database — the index resolves includes file-relative and says
  results may be incomplete;
- crashed tool — keep its output and exit status, clear the busy state, let the
  next queue entry start a fresh process.

Malformed output and protocol errors are reported in the tool output or the
message bar and must never terminate the editor. Index progress is a status
message, not a modal dialog.

## Budgets and acceptance tests

Numbers, so the design can be falsified:

- index throughput ≥ 50 MB/s on the worker thread, with no allocation in the
  lexer's inner loop — **measured at 80 MB/s** over this repository's own
  sources, so the whole tree is indexed in the time one file takes to load;
- ≤ 40 bytes resident per symbol, arena-allocated, no per-symbol heap node;
- single-file reparse under 5 ms for 5,000 lines;
- full index of this repository and platform-h well under one second;
- no per-keystroke work beyond marking one file dirty.

One checked-in acceptance test decides whether the approach works at all: index
this repository, then assert that a fixed list of symbols resolves to the right
scope — a class, an out-of-line member definition, a free function in a
namespace, a method declared inside a class body, a struct. It also holds a
throughput floor, low enough for a loaded machine and high enough to catch a
collapse. That test is what stops the parser rotting, and it came before any UI
was built on the index.

## Delivery order

1. **Lexer.** Token stream and its fixture tests. Nothing depends on anything.
2. **Output parsers, tool runner, `dd.ps1`, error list.** Useful on its own and
   independent of the index.
3. **Declaration parser, symbol table, folder-index integration**, and the
   acceptance test above. If that test disappoints, stop here having lost
   nothing.
4. **Navigation.** Definition with picker, file outline, workspace symbols,
   switch header/source, back/forward history.
5. **CMake model.** Presets, File API, targets, include directories.
6. **Find References** over the existing search, with confidence grouping.
7. **MCP server**, read-only tools first — the write surface is still empty.
8. **Preview-first rename**, then Extract Variable and Extract Lambda.
9. **Check Current File** diagnostics.

Every behaviour belongs in `tests.cpp`, and process launch stays injectable so
unit tests never start PowerShell, CMake or a compiler. Add command-line
diagnostics only where `/test`'s missing message loop makes a real process check
necessary. After any platform-h change, run its suite, push it, and bump
`GIT_TAG` here before relying on the new API.

## Not in the first version

- completion, signature help, hover, or semantic colouring — a lexical
  completion list that is often wrong is worse than none, and member completion
  needs the local declarations inside the function bodies the parser skips;
- debugging and breakpoints;
- package-manager integration;
- parsing other build systems into CMake-like targets;
- automatic build, configure, save, index-on-open beyond the folder index, or
  refactor without confirmation.
