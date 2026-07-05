# Bernini

## Build
```bash
./scripts/build.py --preset <preset> --config <config> <target>
```

Or use Visual Studio.

## Hard Requirements

### CMake

1. If not using Visual Studio download [here](https://cmake.org/download/). Then ensure it is add the directory to PATH.
2. or download CMake extension in Visual Studio Installer if you are using that IDE.


### vcpkg

1. Clone [repo](https://github.com/microsoft/vcpkg) and set **VCPKG_ROOT** environment variable to install location


### python3

1. Download [here](https://www.python.org/downloads/). Ensure **python3** is discoverable.

### Qt

We use Qt for the editor. Get Qt Installer from [here](https://doc.qt.io/qt-6/qt-online-installation.html). In the Qt installer wizard, check Qt for `Development/Qt/Qt x.x.x/MSVC 2022 64-bit` (editor is windows only for now) and uncheck everything else.

## Soft Requirements

### clang format
1. If not using Visual Studio download manually and add to PATH environment variable
2. or download from Visual Studio Installer

## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry (Meshlets)
- Cross Platform (Windows, Linux, Xbox)
