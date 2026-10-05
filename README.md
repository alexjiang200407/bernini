
# Bernini

## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry
- Cross Platform (Windows and MacOS)
- Image Based Lighting
- PBR & Toon Shading
- Custom Shader through Surface Api
- GPU Grass
- Temporal AA and Temporal Upscale
- Animations and Blending
- Blob Shadows
- GPU Crowd Simulation
- Asset Editor with plugin support


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

We use Qt for the editor. Get Qt Installer from [here](https://doc.qt.io/qt-6/qt-online-installation.html). In the Qt installer wizard, check Qt for `Development/Qt/Qt x.x.x/MSVC 2022 64-bit` and uncheck everything else.

### System Requirements
  
**On Windows**

- NVIDIA: Turing or newer — GTX 1660 / RTX 2060 and up.
- AMD: RDNA2 or newer — Radeon RX 6000 series and up (RX 5000/RDNA1 is excluded despite DX12 support).
- Intel: Arc A-series (Alchemist) or newer. Integrated Xe/UHD generally lacks mesh shaders.
- OS: Windows 10+

**MacOS**

- Apple Silicon M1 or later. No Intel Macs
- OS: MacOS 13+

## Soft Requirements

### just

The task runner behind the root `justfile`. `python scripts/init.py` offers to install it, or:

```bash
pip install -r scripts/requirements.txt
```

Skip it if you like; `python scripts/<script>.py` does everything the recipes do.

### clang-format

`just format` and the pre-commit hook run it. `python scripts/init.py` finds it on PATH, in
the Visual Studio LLVM component or in a Homebrew `llvm`, and installs the pinned wheel if
there is none — so this needs nothing done by hand. If yours lives somewhere unusual, give
`init.py` the path when it asks.

