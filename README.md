
# Bernini
<img width="1919" height="973" alt="Screenshot 2026-07-15 013327" src="https://github.com/user-attachments/assets/43cfd696-225b-4005-bce3-073646b83f46" />

## Build

Once per clone, record which preset you build and where your toolchain lives:

```bash
python scripts/init.py
```

That writes `scripts/config.json` (git-ignored — it describes your machine, not
the project; see `scripts/config.example.json`) and offers to install
[just](https://just.systems), the task runner. Everything afterwards reads the
config, so no preset, no configuration and no tool paths need retyping:

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

### CMake

1. If not using Visual Studio download [here](https://cmake.org/download/). Then ensure it is add the directory to PATH.
2. or download CMake extension in Visual Studio Installer if you are using that IDE.


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
render tests compare against — are stored with [Git LFS](https://git-lfs.com). Install it
(`brew install git-lfs`; on Windows it ships with Git for Windows), then let
`python scripts/init.py` configure it for the clone.

It has to be configured **per clone**: the `filter.lfs.*` entries live in local git config,
which no repository can carry. Without them a clone checks out 130-byte pointer files in
place of the assets, and the failure never mentions LFS — the tests report a corrupt
`.glb` ("Invalid magic"), and `git lfs pull` exits 0 having done nothing. To fix an
existing clone by hand:

```bash
git lfs install --local && git lfs pull
```

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

## Soft Requirements

### just

The task runner behind the root `justfile`. `python scripts/init.py` offers to install it, or:

```bash
pip install -r scripts/requirements.txt
```

That pulls `rust-just`, which ships `just` as a prebuilt binary wheel — one command on Windows, Linux and macOS, no Rust toolchain, and the same pinned version for everyone. `winget install Casey.Just`, `brew install just` and `cargo install just` all work too.

Skip it if you like; `python scripts/<script>.py` does everything the recipes do.

### gh (GitHub CLI)

Only needed for the `bcp-revise` AI code-review workflow, which reads PR reviews and posts replies with it (see [docs/ai-coding.md](docs/ai-coding.md)). Not a Python package — the `gh` on PyPI is an unrelated project — so install it from [cli.github.com](https://cli.github.com/) and add it to PATH. `python scripts/init.py` reports whether it is found.

### clang format
1. If not using Visual Studio download manually and add to PATH environment variable
2. or download from Visual Studio Installer
3. or, if it lives somewhere else, run `python scripts/init.py` and give it the path when asked

## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry
- Cross Platform
- Image Based Lighting
- PBR
- Bindless Resources
