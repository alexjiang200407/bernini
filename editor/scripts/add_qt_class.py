#!/usr/bin/env python3
"""Create a new Qt Widgets class: a .ui form plus its C++ .h/.cpp pair.

Generates three consistently-named files for one editor widget class:

    editor/qt/<path>/<Class>.ui      (Designer form; <class> = <Class>)
    editor/src/<path>/<Class>.h
    editor/src/<path>/<Class>.cpp

The class name is the last path component; any leading path becomes a
subdirectory under both qt/ and src/. Existing files are never overwritten.

Examples:
    python editor/scripts/add_qt_class.py Windows/AssetImport/AssetImportWindow --base QMainWindow
    python editor/scripts/add_qt_class.py panels/Outliner
    python editor/scripts/add_qt_class.py dialogs/AboutDialog --base QDialog

After running, reconfigure/build -- the editor's CONFIGURE_DEPENDS globs pick up
the new files and AUTOUIC generates ui_<Class>.h.
"""

import argparse
import os
import re
import sys

# This script lives in editor/scripts/, so two levels up is the editor dir.
EDITOR_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ROOT = os.path.dirname(EDITOR_ROOT)

UI_MAINWINDOW = """\
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>{cls}</class>
 <widget class="QMainWindow" name="{cls}">
  <property name="windowTitle">
   <string>{cls}</string>
  </property>
  <widget class="QWidget" name="centralwidget"/>
 </widget>
 <resources/>
 <connections/>
</ui>
"""

UI_PLAIN = """\
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>{cls}</class>
 <widget class="{base}" name="{cls}">
  <property name="windowTitle">
   <string>{cls}</string>
  </property>
 </widget>
 <resources/>
 <connections/>
</ui>
"""

HEADER = """\
#pragma once

#include <{base}>

#include "ui_{cls}.h"

class {cls} : public {base}
{{
\tQ_OBJECT

public:
\texplicit {cls}(QWidget* parent = nullptr);

private:
\tUi::{cls} m_Ui;
}};
"""

SOURCE = """\
#include "{cls}.h"

{cls}::{cls}(QWidget* parent) : {base}(parent)
{{
\tm_Ui.setupUi(this);
}}
"""

BASES = ("QWidget", "QMainWindow", "QDialog", "QFrame")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="Class path, e.g. Windows/AssetImport/AssetImportWindow")
    ap.add_argument("--base", default="QWidget",
                    help=f"Base widget class (default: QWidget). Common: {', '.join(BASES)}")
    ap.add_argument("--ui-root", default=os.path.join(EDITOR_ROOT, "qt"))
    ap.add_argument("--src-root", default=os.path.join(EDITOR_ROOT, "src"))
    args = ap.parse_args()

    sub, cls = os.path.split(args.name.replace("\\", "/").strip("/"))
    if not re.fullmatch(r"[A-Za-z_]\w*", cls):
        print(f"add_qt_class: invalid class name '{cls}'", file=sys.stderr)
        return 1

    base = args.base
    ui_tmpl = UI_MAINWINDOW if base == "QMainWindow" else UI_PLAIN

    targets = [
        (os.path.join(args.ui_root, sub, cls + ".ui"),   ui_tmpl.format(cls=cls, base=base)),
        (os.path.join(args.src_root, sub, cls + ".h"),   HEADER.format(cls=cls, base=base)),
        (os.path.join(args.src_root, sub, cls + ".cpp"), SOURCE.format(cls=cls, base=base)),
    ]

    existing = [p for p, _ in targets if os.path.exists(p)]
    if existing:
        for p in existing:
            print(f"add_qt_class: refusing to overwrite {p}", file=sys.stderr)
        return 1

    for path, contents in targets:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(contents)
        print(f"add_qt_class: created {os.path.relpath(path, REPO_ROOT)}")

    print("add_qt_class: done - reconfigure/build to pick up the new files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
