"""The editor's text rule, read off the source: what `scripts/tests/test_editor_localization.py` checks.

Two findings. A *raw literal* is a string literal with a letter in it passed straight to a Qt call
that shows text -- what a user would read in English whatever the locale. A *catalog mismatch* is a
localized string -- a `Localize(..., "context.key", "fallback"` call or a descriptor's
`{ "context", "key", "fallback" }` triple -- whose context has no CSV, whose key has no row, or whose
fallback differs from the row's `en`: the English in code and the English on disk disagreeing.

Regex over text, not an AST: it finds the shapes the editor writes and misses a literal reached
through a variable. See docs/editor_plugins.md § Localization.
"""

import csv
import io
import os
import re

import util.cmake_tools as ct

REPO_ROOT = ct.REPO_ROOT

# Files, and directories whose every file is, held to the raw-literal rule. Grows as the sweep lands.
COVERED = [
    "apps/editor/src/main.cpp",
    "apps/editor/src/MainWindow.cpp",
    "apps/editor/src/main_window_ui.cpp",
    "apps/editor/src/Startup",
    "apps/editor/src/Plugins",
    "apps/editor/plugins/default_editor/src/plugin.cpp",
    "examples/editor_plugin",
]

# Where the `{ context, key, fallback }` triples are collected from.
SOURCE_ROOTS = [
    "apps/editor/src",
    "apps/editor/plugins/default_editor/src",
    "apps/editor/plugins/default_editor/include",
    "examples/editor_plugin",
]

# Each module's `localization/`: one `<context>.csv` per context.
CATALOG_DIRS = [
    "apps/editor/localization",
    "apps/editor/plugins/default_editor/localization",
    "examples/editor_plugin/localization",
]

# (repo-relative path, literal) pairs a text call is handed that are not text a user reads.
ALLOWED = set()

SOURCE_EXTENSIONS = (".cpp", ".h")

TEXT_CALLS = [
    # setters and adders on widgets, actions, menus, layouts and models
    "setText", "setToolTip", "setStatusTip", "setWhatsThis", "setWindowTitle",
    "setPlaceholderText", "setTitle", "setLabelText", "setPrefix", "setSuffix",
    "setSpecialValueText", "setTabText", "setItemText", "setHeaderLabel", "setHeaderLabels",
    "setHorizontalHeaderLabels", "setVerticalHeaderLabels", "showMessage",
    "addAction", "addMenu", "addTab", "insertTab", "addRow", "insertRow", "addItem", "addItems",
    "insertItem", "addButton",
    # constructors that take their caption
    "QLabel", "QPushButton", "QCheckBox", "QRadioButton", "QGroupBox", "QToolButton", "QAction",
    "QMenu", "QDockWidget", "QListWidgetItem", "QTreeWidgetItem", "QStandardItem",
    # static dialogs: every string argument is shown
    "QMessageBox::warning", "QMessageBox::critical", "QMessageBox::information",
    "QMessageBox::question", "QMessageBox::about", "QFileDialog::getOpenFileName",
    "QFileDialog::getOpenFileNames", "QFileDialog::getSaveFileName",
    "QFileDialog::getExistingDirectory", "QInputDialog::getText", "QInputDialog::getItem",
    "QInputDialog::getInt", "QInputDialog::getDouble",
]

_CALL = re.compile(
    r"(?<![A-Za-z0-9_])(" + "|".join(re.escape(c) for c in TEXT_CALLS) + r")\s*\(")
_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
_TRIPLE = re.compile(
    r'\{\s*"([a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*)"\s*,\s*"([a-z][a-z0-9_]*)"\s*,\s*'
    r'((?:"(?:[^"\\\n]|\\.)*"\s*)+)\}')
_STRINGS = r'((?:"(?:[^"\\\n]|\\.)*"\s*)+)'
# The resolver argument, when there is one, holds no string literal.
# Groups: context, key, the `{ args }` object when there is one, fallback.
_LOCALIZE = re.compile(
    r'\bLocalize\(\s*[^";]*?"([a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*)\.([a-z][a-z0-9_]*)"\s*,\s*'
    r'(\{(?:[^{};]|\{[^{};]*\})*\}\s*,\s*)?'
    + _STRINGS)
_LETTER = re.compile(r"[A-Za-z]")
_ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "r": "\r"}


def strip_comments(text):
    """`text` with every comment blanked to spaces, so offsets and line numbers still hold."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _arguments(text, open_paren):
    """The text between `open_paren` and its matching `)`, skipping string and char literals."""
    depth, i, n = 0, open_paren, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:i]
        i += 1
    return text[open_paren + 1:]


def decode(literals):
    """The value of one or more adjacent C string literals, as the compiler concatenates them."""
    value = []
    for literal in _LITERAL.findall(literals):
        body, i = literal[1:-1], 0
        while i < len(body):
            if body[i] == "\\" and i + 1 < len(body):
                value.append(_ESCAPES.get(body[i + 1], body[i + 1]))
                i += 2
            else:
                value.append(body[i])
                i += 1
    return "".join(value)


def raw_literals(text):
    """(line, call, literal) for each lettered literal handed to a text call outside a triple."""
    text = strip_comments(text)
    found = []
    for call in _CALL.finditer(text):
        # An argument is data the caller supplies, which may itself be text a user reads.
        args = _LOCALIZE.sub(
            lambda m: "Localize(" + (m.group(3) or ""),
            _TRIPLE.sub("{}", _arguments(text, call.end() - 1)))
        for literal in _LITERAL.findall(args):
            if _LETTER.search(decode(literal)):
                found.append((text.count("\n", 0, call.start()) + 1, call.group(1), literal))
    return found


def triples(text):
    """(line, context, key, fallback) for every localized string in `text`, in source order."""
    text = strip_comments(text)
    found = [(m.start(), m.group(1), m.group(2), m.group(3)) for m in _TRIPLE.finditer(text)]
    found += [(m.start(), m.group(1), m.group(2), m.group(4)) for m in _LOCALIZE.finditer(text)]
    return [
        (text.count("\n", 0, start) + 1, context, key, decode(fallback))
        for start, context, key, fallback in sorted(found)
    ]


def read_catalog(path):
    """{key: en} for one CSV; the `en` column is the one code fallbacks are checked against."""
    with open(path, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(io.StringIO(f.read())))
    header = rows[0]
    en = header.index("en")
    return {row[0]: row[en] for row in rows[1:] if row}


def source_files(roots, repo_root=REPO_ROOT):
    for root in roots:
        path = os.path.join(repo_root, root)
        if os.path.isfile(path):
            yield path
            continue
        for dirpath, _, filenames in os.walk(path):
            for name in sorted(filenames):
                if name.endswith(SOURCE_EXTENSIONS):
                    yield os.path.join(dirpath, name)


def catalogs(dirs=CATALOG_DIRS, repo_root=REPO_ROOT):
    """{context: (csv path, {key: en})} over every catalog directory; a context twice is an error."""
    found, errors = {}, []
    for d in dirs:
        path = os.path.join(repo_root, d)
        if not os.path.isdir(path):
            continue
        for name in sorted(os.listdir(path)):
            if not name.endswith(".csv"):
                continue
            context = name[:-4]
            file = os.path.join(path, name)
            if context in found:
                errors.append(f"{file}: context {context} is also {found[context][0]}")
                continue
            found[context] = (file, read_catalog(file))
    return found, errors


def raw_literal_findings(roots=COVERED, allowed=ALLOWED, repo_root=REPO_ROOT):
    findings = []
    for file in source_files(roots, repo_root):
        relative = os.path.relpath(file, repo_root).replace(os.sep, "/")
        with open(file, encoding="utf-8") as f:
            for line, call, literal in raw_literals(f.read()):
                if (relative, decode(literal)) not in allowed:
                    findings.append(f"{relative}:{line}: {call} is handed {literal}")
    return findings


def catalog_findings(roots=SOURCE_ROOTS, dirs=CATALOG_DIRS, repo_root=REPO_ROOT):
    known, findings = catalogs(dirs, repo_root)
    used = set()
    for file in source_files(roots, repo_root):
        relative = os.path.relpath(file, repo_root).replace(os.sep, "/")
        with open(file, encoding="utf-8") as f:
            for line, context, key, fallback in triples(f.read()):
                where = f"{relative}:{line}"
                if context not in known:
                    findings.append(f"{where}: context {context} has no localization/{context}.csv")
                    continue
                csv_path, rows = known[context]
                used.add((context, key))
                if key not in rows:
                    findings.append(f"{where}: {context}/{key} has no row in {csv_path}")
                elif rows[key] != fallback:
                    findings.append(
                        f"{where}: {context}/{key} falls back to {fallback!r} but the CSV says "
                        f"{rows[key]!r}")
    for context, (csv_path, rows) in known.items():
        for key in rows:
            if (context, key) not in used:
                findings.append(f"{csv_path}: {key} is used by no source")
    return findings
