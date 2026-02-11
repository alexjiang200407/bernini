# Bernini

## Build
```bash
git submodule update --init --recursive
```

```bash
cmake --build --preset preset-name # replace preset-name with chose preset e.g. windows-msvc-debug
```

## Pix

To support PIX debug add the directory where Pix is installed to PATH environment variable.

## Requirements
- CMake
- vcpkg
- python3


## Features
- GPU Driven Instance Rendering
- Virtual Geometry
- Clustered Geometry (Meshlets)
- Cross Platform (Windows, Linux, Xbox)
