# Bernini

## Build
```bash
./scripts/build.py --preset <preset> --config <config> <target>
```

Or use Visual Studio.

## Pix

To support PIX debug add the directory where Pix is installed to PATH environment variable.

## Hard Requirements

### CMake

1. If not using Visual Studio download [here](https://cmake.org/download/). Then ensure it is add the directory to PATH.
2. or download CMake extension in Visual Studio Installer if you are using that IDE.


### vcpkg

1. Clone [repo](https://github.com/microsoft/vcpkg) and set **VCPKG_ROOT** environment variable to install location


### python3

1. Download [here](https://www.python.org/downloads/). Ensure **python3** is discoverable.

## Soft Requirements

### clang format
1. If not using Visual Studio download manually and add to PATH environment variable
2. or download from Visual Studio Installer

## Features
- GPU Driven Instance Rendering
- Forward Renderer
- Clustered Geometry (Meshlets)
- Cross Platform (Windows, Linux, Xbox)
