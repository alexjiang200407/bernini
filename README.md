
# Bernini
<img width="1512" height="982" alt="Screenshot 2026-09-27 at 12 42 27 PM" src="https://github.com/user-attachments/assets/7a07875e-ca86-4cfa-b49d-7baa96b0c48d" />


## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry
- Cross Platform (Windows and MacOS)
- Image Based Lighting
- PBR
- Bindless Resources
- GPU Grass
- Temporal AA and Temporal Upscale
- Animations and Blending
- Blob Shadows


## Build

Clone, then run this once:

```bash
python scripts/init.py
```

The following commands can help build

```bash
just                 # list the commands
just build           # everything
just build editor    # one target
just run editor      # run it, with cwd set to its output dir
```

Any recorded setting can still be overridden per invocation
(`just build --preset <preset> --config <config> <target>`).

## Hard Requirements

`python scripts/init.py` handles every one of these except Python itself, Qt and a shell.
They are written down for the machine it can't finish, and for anyone setting up by hand.

### python3

1. Download [here](https://www.python.org/downloads/). Ensure **python3** is discoverable.

### Bash

The helper scripts and git hooks are driven through a POSIX shell. macOS and Linux ship one; on Windows use Git Bash (bundled with [Git for Windows](https://git-scm.com/download/win)) or WSL.

### Qt

We use Qt for the editor. Get Qt Installer from [here](https://doc.qt.io/qt-6/qt-online-installation.html). In the Qt installer wizard, check Qt for `Development/Qt/Qt x.x.x/MSVC 2022 64-bit` (editor is windows only for now) and uncheck everything else.

### System Requirements
  
**On Windows**

- NVIDIA: Turing or newer — GTX 1660 / RTX 2060 and up.
- AMD: RDNA2 or newer — Radeon RX 6000 series and up (RX 5000/RDNA1 is excluded despite DX12 support).
- Intel: Arc A-series (Alchemist) or newer. Integrated Xe/UHD generally lacks mesh shaders.
- OS: Windows 10+

**MacOS**

- 

## Soft Requirements

### just

The task runner behind the root `justfile`. `python scripts/init.py` offers to install it, or:

```bash
pip install -r scripts/requirements.txt
```

That file is the version registry for all the pinned tooling — `rust-just`, `cmake`,
`ninja`, `clang-format`, `clang-tidy` — each of which ships as a prebuilt binary wheel: one
command on Windows, Linux and macOS, no Rust toolchain and no LLVM install, and the same
version for everyone. `init.py` reads the pins out of it and installs only what is missing,
so use the line above when you want the lot. `winget install Casey.Just`, `brew install
just` and `cargo install just` all work for `just` too.

Skip it if you like; `python scripts/<script>.py` does everything the recipes do.

### clang-format

`just format` and the pre-commit hook run it. `python scripts/init.py` finds it on PATH, in
the Visual Studio LLVM component or in a Homebrew `llvm`, and installs the pinned wheel if
there is none — so this needs nothing done by hand. If yours lives somewhere unusual, give
`init.py` the path when it asks.

### slangd (Slang LSP)

Gives Claude Code go-to-definition, hover and document symbols across the `.slang` sources under
`libs/bgl`, which grep answers badly because every cross-file reference is a module import. Nothing
depends on it: skip this and the build, the tests and the editor are unchanged.

Those three are the whole of it. slangd advertises no `referencesProvider` and no
`workspaceSymbolProvider`, so find-references and workspace-symbol are errors rather than empty
results, and grep remains the way to ask who uses a thing.

There is nothing to install. `slangd` is the language server behind the official Slang editor
extensions, and the vcpkg `shader-slang` port already puts it beside `slangc`:

```
build/<preset>/vcpkg_installed/<triplet>/tools/shader-slang/slangd
```

Put that directory on PATH, then turn on the plugin this repo carries:

```bash
claude plugin marketplace add ./     # the trailing slash is required
claude plugin install slang-lsp@bernini
```
