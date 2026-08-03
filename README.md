
# Bernini
<img width="1919" height="973" alt="Screenshot 2026-07-15 013327" src="https://github.com/user-attachments/assets/43cfd696-225b-4005-bce3-073646b83f46" />

## Build

Clone, then run this once:

```bash
python scripts/init.py
```

It sets the machine up rather than telling you to: it finds or clones and bootstraps
**vcpkg**, installs any of **cmake**, **ninja**, **clang-format**, **clang-tidy** and
[**just**](https://just.systems) that are missing from the versions pinned in
`scripts/requirements.txt`, offers **git-lfs** and **gh** through winget/brew, configures
**Git LFS** for the clone and fetches any asset still left as a pointer, and points git at
the committed hooks. Whatever it finds already installed it keeps.

Then it asks which preset you build and writes `scripts/config.json` (git-ignored — it
describes your machine, not the project; `scripts/util/config.py` documents its
schema). Everything
afterwards reads that config, so no preset, no configuration, no tool path and no
`VCPKG_ROOT` need retyping:

```bash
just                 # list the commands
just build           # everything
just build editor    # one target
just run editor      # run it, with cwd set to its output dir
```

Any recorded setting can still be overridden per invocation
(`just build --preset <preset> --config <config> <target>`).

`just` is a convenience layer, not a requirement: every recipe is a one-line call
into `scripts/`, so `python scripts/build.py <target>` does the same thing in a
clone that hasn't installed it. Or use Visual Studio.

## Hard Requirements

`python scripts/init.py` handles every one of these except Python itself, Qt and a shell.
They are written down for the machine it can't finish, and for anyone setting up by hand.

### python3

1. Download [here](https://www.python.org/downloads/). Ensure **python3** is discoverable.

### CMake

`init.py` installs the pinned `cmake` wheel when it finds none — no admin rights, no PATH
surgery. Otherwise: download [here](https://cmake.org/download/) and add it to PATH, or
take the CMake component in the Visual Studio Installer, which `init.py` also finds.

### vcpkg

`init.py` looks for a checkout (`VCPKG_ROOT`, `~/vcpkg`, `C:\vcpkg`, `/opt/vcpkg`, …) and
offers to clone and bootstrap one when there is none. It records the path in
`scripts/config.json` and every build exports it as `VCPKG_ROOT`, so the environment
variable is not something you have to set.

Which vcpkg it is does not matter — `vcpkg.json` pins `builtin-baseline`, so the package
versions come from the manifest. It must, though, be a **full clone**: a `--depth 1` one
does not contain the baseline commit vcpkg has to resolve.

### Git LFS

The assets under `assets/` — meshes, textures, environment maps, the golden images the
render tests compare against — are stored with [Git LFS](https://git-lfs.com).

It has to be configured **per clone**: the `filter.lfs.*` entries live in local git config,
which no repository can carry. Without them a clone checks out 130-byte pointer files in
place of the assets, and the failure never mentions LFS — the tests report a corrupt
`.glb` ("Invalid magic"), and `git lfs pull` exits 0 having done nothing. `init.py`
installs git-lfs, configures the filters and refetches anything left as a pointer; after
that, `just run` and `just test` refuse to launch a binary against a pointer checkout and
say exactly this. To fix a clone by hand:

```bash
git lfs install --local && git lfs pull
```

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

### gh (GitHub CLI)

Only needed for the `bcp-revise` AI code-review workflow, which reads PR reviews and posts replies with it (see [docs/ai-coding.md](docs/ai-coding.md)). Not a Python package — the `gh` on PyPI is an unrelated project — so `python scripts/init.py` offers it through winget or brew, or install it from [cli.github.com](https://cli.github.com/) and add it to PATH.

### clang-format

`just format` and the pre-commit hook run it. `python scripts/init.py` finds it on PATH, in
the Visual Studio LLVM component or in a Homebrew `llvm`, and installs the pinned wheel if
there is none — so this needs nothing done by hand. If yours lives somewhere unusual, give
`init.py` the path when it asks.

## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry
- Cross Platform
- Image Based Lighting
- PBR
- Bindless Resources
