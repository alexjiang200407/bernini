# editor

editor is the Bernini game editor: a desktop application for authoring scenes and
managing resources. It is also the offline asset-cook host — artists export glTF, the
editor imports it (via assetlib) and converts it into the game-ready format.

- CMake target: `editor`. Built **automatically only when Qt6 is found** — the root
  `CMakeLists.txt` probes `find_package(Qt6 ...)`; there is no manual `BUILD_EDITOR` flag.
- Windows-only for now.
- CMake: `./CMakeLists.txt`

# UI: Qt Widgets, not QML

The UI is **Qt Widgets**, authored by drag-and-drop in **Qt Designer** (`.ui` files).
QML/Qt Quick was deliberately not used: the editor is a docked, dense-widget tool
(Outliner, Details, Content Browser around a 3D viewport), which is Widgets territory.

- `./src` — C++ (logic, behaviour, models, and any dynamic / runtime-built UI).
- `./qt`  — `.ui` files (Designer-owned layout), organised per component

## Rules

- Qt is editor only don't link to other targets
- **Generated `ui_*.h` and moc files are build artifacts** in the build tree — never
  edit or commit them; just `#include "ui_<Name>.h"`.


