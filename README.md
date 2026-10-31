# dde

[![Build](https://github.com/ZacWalk/dde/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/dde/actions/workflows/build.yml)

dde, the editor for [dd](https://github.com/ZacWalk/dd) projects. Open a folder
that dd builds, edit it, and build, test, run and launch it from the keyboard,
with the output streaming into a pane beside the code and every error one key
away.

Written in C++ and rendered with plain GDI. The only dependency is [libdesktop](https://github.com/ZacWalk/libdesktop), the Win32 abstraction it shares with its sibling apps — nothing third-party. It starts instantly and typically uses a few megabytes of memory.

![dde editing its own source, with a dd build in the Output pane](screenshot.png)

*dde with its own repository open: a source file in the editor and `dd build debug` in the Output pane.*

## Features

- **dd built in** — Build (`F7`), Test (`Ctrl+Shift+T`), Run (`Ctrl+F5`) and Launch (`Ctrl+Shift+F5`) from the **dd** menu, with the configuration, the test label, every target and the project's own `dd.psd1` commands a click away. dd.psd1 is read as data; nothing runs because a folder was opened
- **Output pane** — the running command's output, live (`Ctrl+Shift+U`); double-click an error, or press `F8` / `Shift+F8`, to go to its file, line and column
- **Folder browser** — navigate a root folder; create, rename and delete files inline
- **Multi-file search** — live search across the folder, grouped by file with highlighted matches (`Ctrl+Shift+F`)
- **Four document views** — text, Markdown, CSV table and hex, chosen automatically and remembered per file
- **Syntax highlighting** — C++, Rust, Python, PowerShell and Markdown
- **C/C++ navigation** — go to definition (`F12`), switch header/source (`Ctrl+F12`), and navigation history (`Alt+Left` / `Alt+Right`), using a lightweight lexical index rather than a compiler
- **Editing** — unlimited undo, word wrap, indent/unindent, spell check
- **Utilities** — JSON reformat (`Ctrl+R`), sort and de-duplicate lines, evaluate a selected expression (`Ctrl+E`)
- **Session memory** — restores the recent root folders and the last document open in each

Open documents are never closed behind your back: unsaved files stay in memory with their full undo history and are shown in red. You are prompted to save only when changing folder, exiting, or running a dd command.

dd itself needs [PowerShell 7.4](https://github.com/PowerShell/PowerShell) (`pwsh`) on `PATH`; without it, or without a `dd.ps1` in the folder, the dd menu says so and stays disabled.

## Building

Requires Windows x64 and Visual Studio with the Desktop C++ workload. The
vendored [dd 0.2.5](https://github.com/ZacWalk/dd/releases/tag/v0.2.5) runtime
locates Visual Studio and uses the CMake and Ninja that ship with it.

```powershell
.\dd.ps1 build        # both configurations
.\dd.ps1 test --label unit
.\dd.ps1 launch       # build, then start a persistent app
```

Output is `exe\dde-64.exe`, or `dde-64d.exe` for Debug. Neither `run` nor
`launch` closes an already-running copy, so close the app before rebuilding its
executable — or build the other configuration from inside it.

`FetchContent` brings in [libdesktop](https://github.com/ZacWalk/libdesktop).
A sibling `../libdesktop` checkout is used automatically
when present; a clean clone otherwise fetches the `GIT_TAG` pinned in
`CMakeLists.txt`.

## Documentation

- [docs/design.md](docs/design.md) — architecture, views, keyboard reference and configuration
- [docs/cpp.md](docs/cpp.md) — the lexical C/C++ index behind go-to-definition
- [AGENTS.md](AGENTS.md) — conventions for contributors

## License

[MIT](LICENSE). The executable links libdesktop (LGPL-2.1-or-later)
statically, so a distributed binary must let its users relink it against a
modified libdesktop (LGPL-2.1 section 6). Before distributing a binary,
include libdesktop's notices, licence and corresponding source, including
changes, plus the application source and build instructions or relinkable
object files. Record the exact revisions, compiler and configuration, and
verify a build with a modified libdesktop. Permit modification for the
recipient's own use and reverse engineering to debug those modifications.
A link to the latest source alone is not a relinking package.
