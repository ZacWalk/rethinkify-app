# Rethinkify

[![Build](https://github.com/ZacWalk/noterad/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/noterad/actions/workflows/build.yml)

A lightweight Windows text editor for working across a folder full of notes, logs and data files. Point it at a folder, search everything in it, and read or edit whatever comes back.

Written in C++ and rendered with plain GDI. The only dependency is [platform-h](https://github.com/ZacWalk/platform-h), the Win32 abstraction it shares with its sibling apps — nothing third-party. It starts instantly and typically uses a few megabytes of memory.

Still a work in progress. (for about 10 years)

![Rethinkify screenshot](screenshot.png)

## Features

- **Folder browser** — navigate a root folder; create, rename and delete files inline
- **Multi-file search** — live search across the folder, grouped by file with highlighted matches (`Ctrl+Shift+F`)
- **Four document views** — text, Markdown, CSV table and hex, chosen automatically and remembered per file
- **Syntax highlighting** — C++, Rust, Python, PowerShell and Markdown
- **Editing** — unlimited undo, word wrap, indent/unindent, spell check
- **Utilities** — JSON reformat (`Ctrl+R`), sort and de-duplicate lines, evaluate a selected expression (`Ctrl+E`)
- **Session memory** — restores the recent root folders and the last document open in each

Open documents are never closed behind your back: unsaved files stay in memory with their full undo history and are shown in red. You are prompted to save only when changing folder or exiting.

## Building

Requires Windows x64 and Visual Studio with the Desktop C++ workload. The
vendored [dd](https://github.com/ZacWalk/dd) runtime locates Visual Studio and
uses the CMake and Ninja that ship with it.

```powershell
.\dd.ps1 build        # both configurations
.\dd.ps1 test         # build and run the suite
.\dd.ps1 run          # build, then launch
```

Output is `exe\rethinkify-64.exe`, or `rethinkify-64d.exe` for Debug. The
platform layer comes from [platform-h](https://github.com/ZacWalk/platform-h) via
`FetchContent`; a sibling `../platform-h` checkout is used automatically when
present.

## Documentation

- [docs/design.md](docs/design.md) — architecture, views, keyboard reference and configuration
- [AGENTS.md](AGENTS.md) — conventions for contributors and coding agents

## License

[MIT](LICENSE)
