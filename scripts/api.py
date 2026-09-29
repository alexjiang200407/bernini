#!/usr/bin/env python3
"""Generate the API catalog: every public symbol, for an agent to search and a person to browse.

Usage:
    python scripts/api.py                  # refresh build/api/ if a public header changed
    python scripts/api.py --force          # regenerate whatever the stamp says
    python scripts/api.py --library game=src --out build/api   # another tree's roots

An agent asked to write a helper cannot search for one it does not suspect exists, and the
duplicate is never called the same thing. The catalog lists what the public headers offer -- each
symbol's qualified name, its declaration, the first sentence of its doc comment and where it is --
so the search can be for what a function does rather than what it might be called. See
docs/api_catalog.md.

Written under build/api/, never committed: every checkout, feature worktrees included, generates
its own from its own headers, so there is nothing to merge.

    INDEX.md            the libraries, their counts, and how to search them
    <library>.md        one line per symbol, grouped by header -- what an agent greps
    html/index.html     one self-contained page to browse, for people

The headers are parsed with libclang, not read with regexes: templates, overloads and qualified
names are what make a line findable, and only a compiler gets those right. One translation unit per
library includes every public header, compiled with the flags compile_commands.json records for
that library, with function bodies skipped. The libclang loaded is the build toolchain's own where
one can be found, since its builtin headers must match the standard library the flags name -- the
pip wheel's bundled copy has none, and is only the fallback.
"""

import argparse
import glob
import hashlib
import html
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

import util.cmake_tools as ct

DEFAULT_OUT = os.path.join(ct.REPO_ROOT, "build", "api")
STAMP = ".stamp.json"
CACHE = ".symbols"

# Namespaces whose contents are public by the letter of the include rule but not meant to be called.
PRIVATE_NAMESPACES = {"detail", "details", "impl", "internal"}

BRIEF_LIMIT = 200
DECLARATION_LIMIT = 240


class CatalogError(Exception):
    """The catalog could not be generated. The message says what to do about it."""


# --- Libraries ------------------------------------------------------------------

def default_libraries(root=ct.REPO_ROOT):
    """{name: include dir} for every `libs/<name>/include` under `root`."""
    found = {}
    for include in sorted(glob.glob(os.path.join(root, "libs", "*", "include"))):
        if os.path.isdir(include):
            found[os.path.basename(os.path.dirname(include))] = os.path.normpath(include)
    return found


def public_headers(include_dir):
    """Every header under `include_dir`, sorted, as absolute paths."""
    headers = []
    for pattern in ("*.h", "*.hpp"):
        headers += glob.glob(os.path.join(include_dir, "**", pattern), recursive=True)
    return sorted(os.path.normpath(h) for h in headers)


def script_digest():
    """Content hash of this script: a new one may write any line differently."""
    with open(os.path.abspath(__file__), "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def library_digest(include_dir):
    """Content hash of one library's public headers, their paths included."""
    digest = hashlib.sha256()
    for header in public_headers(include_dir):
        digest.update(os.path.relpath(header, include_dir).replace("\\", "/").encode())
        with open(header, "rb") as fh:
            digest.update(fh.read())
    return digest.hexdigest()


# --- Compile flags --------------------------------------------------------------

def _is_cl(compiler):
    name = os.path.basename(compiler).lower()
    return name in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")


# Options that carry what a header needs to parse: where its includes are, what is defined, which
# language. Everything else -- warnings, codegen, the PCH, the output -- is dropped, since it either
# does not affect a declaration or names a file that only makes sense to the real build.
_GNU_WITH_VALUE = ("-I", "-isystem", "-iquote", "-idirafter", "-F", "-iframework", "-D", "-U", "-isysroot",
                   "--sysroot", "-target", "-arch")
_GNU_JOINED = ("-I", "-F", "-D", "-U", "-std=", "--std=", "-isystem", "-iframework", "--sysroot=", "--target=")
_CL_WITH_VALUE = ("/I", "-I", "/D", "-D", "/U", "-U", "/external:I", "-external:I", "-imsvc")
_CL_JOINED = ("/I", "-I", "/D", "-D", "/U", "-U", "/std:", "-std:", "/external:I", "-external:I",
              "/Zc:", "-Zc:", "-imsvc")
_CL_EXACT = ("/permissive-", "-permissive-", "/EHsc", "-EHsc")


def parse_flags(entry):
    """The compiler and the flags a header parse needs, from one compile_commands.json entry.

    Returns (compiler, flags). A cl-style command keeps cl's spelling and gets `--driver-mode=cl`,
    which is how libclang reads `/I` and `/std:c++20` without a translation table here.
    """
    # POSIX quoting eats the backslashes of a Windows path, so a command holding any is split the
    # way cmd.exe would.
    command = entry.get("command", "")
    args = entry.get("arguments") or shlex.split(command, posix="\\" not in command)
    compiler, rest = args[0], args[1:]
    cl = _is_cl(compiler)
    with_value = _CL_WITH_VALUE if cl else _GNU_WITH_VALUE
    joined = _CL_JOINED if cl else _GNU_JOINED

    flags = ["--driver-mode=cl"] if cl else []
    i = 0
    while i < len(rest):
        arg = rest[i]
        if arg in with_value and i + 1 < len(rest):
            flags += [arg, rest[i + 1]]
            i += 2
            continue
        if cl and arg in _CL_EXACT:
            flags.append(arg)
        elif arg.startswith(joined) and arg not in with_value:
            flags.append(arg)
        i += 1
    if cl:
        # A newer MSVC STL refuses an older clang outright; the declarations are what is wanted.
        flags.append("/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
    return compiler, flags


def _under(path, directory):
    path, directory = os.path.normcase(os.path.normpath(path)), os.path.normcase(os.path.normpath(directory))
    return path == directory or path.startswith(directory + os.sep)


def entry_for(entries, include_dir):
    """The compile_commands.json entry whose flags parse the library at `include_dir`, or None.

    A source in the library's own `src/` is best: its flags are the ones the headers were written
    against. A library without one -- header-only, or holding only tests -- takes the translation
    unit whose flags name its include dir and name the most, since a consumer that sees more of the
    tree is likelier to resolve everything the headers include. A test of the library itself is no
    better: editor_plugin_api's tests are built without the Qt widgets its EditorPanel.h includes.
    """
    library_dir = os.path.dirname(include_dir)

    def source(entry):
        return os.path.join(entry.get("directory", ""), entry["file"])

    own_src = [e for e in entries if _under(source(e), os.path.join(library_dir, "src"))]
    if own_src:
        return own_src[0]

    needle = os.path.normcase(os.path.normpath(include_dir))
    best, best_flags = None, -1
    for entry in entries:
        _, flags = parse_flags(entry)
        names_it = any(needle in os.path.normcase(os.path.normpath(flag)) for flag in flags)
        if names_it and len(flags) > best_flags:
            best, best_flags = entry, len(flags)
    return best


def load_compile_db(path):
    if not path:
        raise CatalogError("the preset has no build dir, so there is no compile database to read.")
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except OSError:
        raise CatalogError(f"no compile database at {path}. The catalog is parsed with the build's "
                           f"flags, so it needs a configured build dir of a Ninja preset: run `just build`.")


# --- libclang -------------------------------------------------------------------

def _libclang_names():
    if sys.platform == "win32":
        return ("libclang.dll",)
    if sys.platform == "darwin":
        return ("libclang.dylib",)
    return ("libclang.so", "libclang.so.1")


def _libclang_beside(compiler):
    """libclang shipped with the toolchain that holds `compiler`, or None."""
    path = shutil.which(compiler) or compiler
    if sys.platform == "darwin" and os.path.dirname(os.path.realpath(path)) == "/usr/bin":
        # /usr/bin/clang++ is xcrun's shim; the toolchain it runs is elsewhere.
        try:
            path = subprocess.run(["xcrun", "--find", "clang"], capture_output=True, text=True,
                                  check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return None
    return _libclang_in(os.path.dirname(os.path.realpath(path)))


def _libclang_in(bin_dir):
    """The libclang an LLVM install whose programs are in `bin_dir` ships, or None: beside them on
    Windows, in the sibling `lib/` elsewhere."""
    for directory in (bin_dir, os.path.join(os.path.dirname(bin_dir), "lib")):
        for name in _libclang_names():
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate):
                return candidate
    return None


def find_libclang(compiler):
    """The libclang to parse with: $BERNINI_LIBCLANG, the build compiler's, a known LLVM's, or None.

    None means the pip wheel's bundled copy, which parses, but trips over a standard library newer
    than it is -- so the caller warns.
    """
    override = os.environ.get("BERNINI_LIBCLANG")
    if override:
        if not os.path.isfile(override):
            raise CatalogError(f"BERNINI_LIBCLANG is {override}, which does not exist.")
        return override
    if not _is_cl(compiler) or "clang" in os.path.basename(compiler).lower():
        found = _libclang_beside(compiler)
        if found:
            return found
    for bin_dir in ct.llvm_bin_dirs():
        found = _libclang_in(bin_dir)
        if found:
            return found
    return None


def load_cindex(library_file):
    try:
        import clang.cindex as cindex
    except ImportError:
        raise CatalogError("the libclang Python bindings are missing: pip install -r scripts/requirements.txt "
                           "(or `just init`).")
    if library_file and not cindex.Config.loaded:
        cindex.Config.set_library_file(library_file)
    return cindex


# --- Extraction -----------------------------------------------------------------

def clean_comment(raw):
    """A raw comment's text without its `/**`, `*`, `///` and `//` framing, paragraphs kept."""
    if not raw:
        return ""
    lines = []
    for line in raw.strip().splitlines():
        line = line.strip()
        line = re.sub(r"^/\*\*?!?<?", "", line)
        line = re.sub(r"\*/$", "", line)
        line = re.sub(r"^///?!?<?", "", line)
        line = re.sub(r"^\*(?!\*)", "", line)
        lines.append(line.strip())
    text = "\n".join(lines).strip()
    return re.sub(r"\n{3,}", "\n\n", text)


def first_sentence(text):
    """The comment's first sentence, on one line: what the index shows.

    A leading `@param` or `@return` block says how to call it, not what it is for, so the sentence
    comes from the first paragraph that is not a block command; a comment that is nothing else has
    none.
    """
    paragraphs = [p for p in text.split("\n\n") if p.strip() and not p.lstrip().startswith(("@", "\\"))]
    if not paragraphs:
        return ""
    paragraph = paragraphs[0].replace("\n", " ")
    paragraph = re.sub(r"\s+", " ", paragraph).strip()
    match = re.search(r"(?<!\be\.g)(?<!\bi\.e)(?<!\bvs)[.!?](\s|$)", paragraph)
    sentence = paragraph[:match.end()].strip() if match else paragraph
    if len(sentence) > BRIEF_LIMIT:
        sentence = sentence[:BRIEF_LIMIT - 1].rstrip() + "…"
    return sentence


def declaration_text(lines, start, end):
    """The declaration's source from `start` to its body or its semicolon, on one line.

    Taken from the header rather than rebuilt from the AST, so it reads the way its author wrote
    it -- `[[nodiscard]]`, `noexcept`, the template head, default arguments -- which is what someone
    deciding whether to call it wants.
    """
    (line0, col0), (line1, col1) = start, end
    chunk = lines[line0 - 1:line1]
    if not chunk:
        return ""
    if len(chunk) == 1:
        chunk = [chunk[0][col0 - 1:col1 - 1]]
    else:
        chunk[0] = chunk[0][col0 - 1:]
        chunk[-1] = chunk[-1][:col1 - 1]
    text = "\n".join(chunk)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)

    depth = 0
    for i, ch in enumerate(text):
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth = max(depth - 1, 0)
        elif ch in "{;" and depth == 0:
            text = text[:i]
            break
        elif (ch == ":" and depth == 0 and text[i + 1:i + 2] != ":" and text[i - 1:i] != ":"
              and text[:i].rstrip().endswith(")")):
            text = text[:i]  # a constructor's member initializer list
            break
    text = re.sub(r"\s+", " ", text).strip()
    text = re.sub(r"\s*=\s*$", "", text)
    if len(text) > DECLARATION_LIMIT:
        text = text[:DECLARATION_LIMIT - 1].rstrip() + "…"
    return text


class Extractor:
    """Walks one library's translation unit and collects the symbols its own headers declare."""

    def __init__(self, cindex, include_dir):
        self.ci = cindex
        self.include_dir = include_dir
        self.symbols = []
        self._seen = set()
        self._lines = {}
        K = cindex.CursorKind
        self.records = {K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.UNION_DECL}
        self.functions = {K.FUNCTION_DECL, K.FUNCTION_TEMPLATE}
        self.members = {K.CXX_METHOD, K.FUNCTION_TEMPLATE, K.CONSTRUCTOR, K.CONVERSION_FUNCTION}
        self.others = {K.ENUM_DECL, K.TYPE_ALIAS_DECL, K.TYPEDEF_DECL, K.VAR_DECL,
                       getattr(K, "TYPE_ALIAS_TEMPLATE_DECL", None), getattr(K, "CONCEPT_DECL", None)}
        self.others.discard(None)
        self.kind_names = {K.CLASS_DECL: "class", K.STRUCT_DECL: "struct", K.CLASS_TEMPLATE: "class",
                           K.UNION_DECL: "union", K.ENUM_DECL: "enum", K.TYPE_ALIAS_DECL: "type",
                           K.TYPEDEF_DECL: "type", K.VAR_DECL: "constant", K.CONSTRUCTOR: "constructor"}
        for name in ("TYPE_ALIAS_TEMPLATE_DECL", "CONCEPT_DECL"):
            if hasattr(K, name):
                self.kind_names[getattr(K, name)] = "concept" if name == "CONCEPT_DECL" else "type"

    def _file_lines(self, path):
        if path not in self._lines:
            with open(path, encoding="utf-8", errors="replace") as fh:
                self._lines[path] = fh.read().splitlines()
        return self._lines[path]

    def _own(self, cursor):
        location = cursor.location
        return location.file is not None and _under(location.file.name, self.include_dir)

    def _qualified(self, cursor):
        parts = []
        node = cursor
        while node is not None and node.kind != self.ci.CursorKind.TRANSLATION_UNIT:
            if node.spelling:
                parts.append(node.spelling)
            node = node.semantic_parent
        return "::".join(reversed(parts))

    def _is_deleted(self, cursor):
        check = getattr(cursor, "is_deleted_method", None)
        try:
            return bool(check and check())
        except Exception:
            return False

    def _record(self, cursor, kind, parent=None):
        # A USR alone is not unique: two constrained constructor templates of one class can share it.
        key = (cursor.get_usr(), cursor.location.file.name, cursor.location.line, cursor.spelling)
        if key in self._seen:
            return
        self._seen.add(key)
        path = os.path.normpath(cursor.location.file.name)
        lines = self._file_lines(path)
        extent = cursor.extent
        if extent.start.line:
            span = (extent.start.line, extent.start.column), (extent.end.line, extent.end.column)
        else:
            # libclang gives an abbreviated function template (`f(std::integral auto v)`) no
            # extent; the line its name is on holds the declaration.
            line = cursor.location.line
            span = (line, 1), (line, len(lines[line - 1]) + 1)
        declaration = declaration_text(lines, *span)
        doc = clean_comment(cursor.raw_comment)
        self.symbols.append({
            "name": self._qualified(cursor),
            "kind": kind,
            "declaration": declaration,
            "brief": first_sentence(doc),
            "doc": doc,
            "header": os.path.relpath(path, self.include_dir).replace("\\", "/"),
            "path": path,
            "line": cursor.location.line,
            "parent": parent,
        })

    def walk(self, cursor):
        K = self.ci.CursorKind
        for child in cursor.get_children():
            if child.kind == K.NAMESPACE:
                if child.spelling and child.spelling not in PRIVATE_NAMESPACES:
                    self.walk(child)
                continue
            if not self._own(child):
                continue
            if child.kind in self.records:
                if child.is_definition() and child.spelling:
                    self._record(child, self.kind_names.get(child.kind, "class"))
                    self._walk_members(child)
            elif child.kind in self.functions:
                if child.spelling.startswith("operator"):
                    continue
                self._record(child, "function")
            elif child.kind in self.others and child.spelling:
                if child.kind == K.ENUM_DECL and not child.is_definition():
                    continue
                self._record(child, self.kind_names.get(child.kind, "type"))

    def _walk_members(self, record):
        K = self.ci.CursorKind
        parent = self._qualified(record)
        for member in record.get_children():
            if member.access_specifier != self.ci.AccessSpecifier.PUBLIC or not self._own(member):
                continue
            if member.kind in self.records:
                if member.is_definition() and member.spelling:
                    self._record(member, self.kind_names.get(member.kind, "class"), parent)
                    self._walk_members(member)
            elif member.kind in self.members:
                if member.spelling.startswith("operator") or self._is_deleted(member):
                    continue
                constructor = member.kind == K.CONSTRUCTOR or member.spelling == record.spelling
                if member.kind == K.CONSTRUCTOR and _is_copy_or_move(member, record):
                    continue
                self._record(member, "constructor" if constructor else "method", parent)
            elif member.kind in (K.ENUM_DECL, K.TYPE_ALIAS_DECL, K.TYPEDEF_DECL) and member.spelling:
                self._record(member, self.kind_names.get(member.kind, "type"), parent)
            elif member.kind == K.FIELD_DECL and member.spelling:
                self._record(member, "field", parent)
            elif member.kind == K.VAR_DECL and member.spelling:
                self._record(member, "constant", parent)


def _is_copy_or_move(constructor, record):
    params = [c for c in constructor.get_arguments()]
    if len(params) != 1:
        return False
    spelled = params[0].type.spelling.replace("const ", "").rstrip("&").strip()
    return spelled == record.spelling or spelled.endswith("::" + record.spelling)


def extract_library(cindex, include_dir, flags):
    """(symbols, errors) for one library: its public headers parsed as one translation unit."""
    headers = public_headers(include_dir)
    if not headers:
        return [], []
    source = "".join(f'#include "{h}"\n' for h in headers)
    tu_name = os.path.join(include_dir, "__bernini_api_catalog__.cpp")
    options = (cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
               | cindex.TranslationUnit.PARSE_INCOMPLETE)
    driver = [] if "--driver-mode=cl" in flags else ["-x", "c++"]
    tu = cindex.Index.create().parse(tu_name, args=driver + flags + ["-fparse-all-comments"],
                                     unsaved_files=[(tu_name, source)], options=options)
    errors = [f"{d.location.file.name if d.location.file else '?'}:{d.location.line}: {d.spelling}"
              for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    extractor = Extractor(cindex, include_dir)
    extractor.walk(tu.cursor)
    return extractor.symbols, errors


# --- Rendering ------------------------------------------------------------------

def _repo_relative(path, root):
    try:
        return os.path.relpath(path, root).replace("\\", "/")
    except ValueError:
        return path.replace("\\", "/")


def _by_header(symbols):
    grouped = {}
    for symbol in symbols:
        grouped.setdefault(symbol["header"], []).append(symbol)
    return grouped


def _index_line(symbol, root, indent=""):
    brief = f" — {symbol['brief']}" if symbol["brief"] else ""
    where = f"{_repo_relative(symbol['path'], root)}:{symbol['line']}"
    return f"{indent}- `{symbol['name']}` ({symbol['kind']}) `{symbol['declaration']}`{brief} — {where}"


def render_library_markdown(name, include_dir, symbols, root):
    grouped = _by_header(symbols)
    out = [f"# {name} — public API",
           "",
           f"{len(symbols)} symbols in {len(grouped)} headers, from "
           f"`{_repo_relative(include_dir, root)}`. Generated by `just api`; never edit, never commit.",
           "One line per symbol: qualified name, kind, declaration, first sentence of its doc comment, "
           "and where it is. The header is the source of truth.",
           ""]
    for header in sorted(grouped):
        out += [f"## {header}", ""]
        for symbol in grouped[header]:
            out.append(_index_line(symbol, root, "  " if symbol["parent"] else ""))
        out.append("")
    return "\n".join(out)


def render_index_markdown(summary, root):
    out = ["# API catalog",
           "",
           "Every public symbol of every library, generated from the public headers by `just api` "
           "(and by `just build` whenever a public header changed). Never edit, never commit.",
           "",
           "**Before writing a helper, search here for what it would do** -- as well as, never instead "
           "of, your usual search of the code: a symbol with no doc comment is listed by its "
           "declaration alone, so a miss here proves nothing. Search by behaviour, not by the name you "
           "would give it, in whole words, and read every hit rather than the first few: "
           "`grep -iwE 'ceil|round up' build/api/*.md` (a bare `ring` also matches every `string`). "
           "Each hit is one line with the qualified name, the declaration, the first sentence of its "
           "doc comment and `path:line`; open the header before calling it.",
           "",
           "| Library | Symbols | Documented | File |",
           "|---|---|---|---|"]
    for name, info in sorted(summary.items()):
        out.append(f"| `{name}` | {info['symbols']} | {info['documented']} | [{name}.md]({name}.md) |")
    out += ["", "For people: [html/index.html](html/index.html).", ""]
    return "\n".join(out)


_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>API catalog</title>
<style>
:root { --bg:#fbfbfa; --panel:#f1f0ec; --text:#1d1d1b; --muted:#6b6a65; --line:#dcdad3; --accent:#2f5d8a; --code:#f6f5f1; }
@media (prefers-color-scheme: dark) { :root { --bg:#161615; --panel:#1f1f1d; --text:#e8e6e0; --muted:#9a988f; --line:#33322e; --accent:#8fb8e0; --code:#1b1b19; } }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:15px/1.5 -apple-system, "Segoe UI", system-ui, sans-serif; display:grid; grid-template-columns: 280px 1fr; height:100vh; }
nav { background:var(--panel); border-right:1px solid var(--line); overflow:auto; padding:12px; }
main { overflow:auto; padding:20px 28px; }
input { width:100%; padding:8px 10px; border:1px solid var(--line); border-radius:6px; background:var(--bg); color:var(--text); font:inherit; }
nav h2 { font-size:13px; text-transform:uppercase; letter-spacing:.04em; color:var(--muted); margin:16px 0 4px; cursor:pointer; }
nav a { display:block; color:var(--text); text-decoration:none; font:13px/1.7 ui-monospace, Menlo, Consolas, monospace; padding-left:8px; border-radius:4px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
nav a:hover, nav a.on { background:var(--line); }
.count { color:var(--muted); font-size:12px; margin:8px 0 0; }
h1 { font-size:20px; margin:0 0 4px; }
.card { border:1px solid var(--line); border-radius:8px; padding:12px 14px; margin:12px 0; background:var(--bg); }
.card.member { margin-left:28px; }
.name { font:600 14px ui-monospace, Menlo, Consolas, monospace; overflow-wrap:anywhere; }
.kind { color:var(--muted); font-size:12px; margin-left:8px; }
pre { background:var(--code); border:1px solid var(--line); border-radius:6px; padding:8px 10px; margin:8px 0; white-space:pre-wrap; overflow-wrap:anywhere; font:13px/1.45 ui-monospace, Menlo, Consolas, monospace; }
.doc { white-space:pre-wrap; margin:6px 0 0; }
.nodoc { color:var(--muted); font-style:italic; }
.where { color:var(--muted); font:12px ui-monospace, Menlo, Consolas, monospace; }
.where a { color:var(--accent); }
@media (max-width: 760px) { body { grid-template-columns: 1fr; height:auto; } nav { max-height:40vh; } main { padding:16px; } }
</style>
</head>
<body>
<nav>
  <input id="q" type="search" placeholder="Search names, declarations, docs" autofocus>
  <p class="count" id="count"></p>
  <div id="tree"></div>
</nav>
<main id="main"></main>
<script>
const DATA = __DATA__;
const tree = document.getElementById('tree'), main = document.getElementById('main');
const q = document.getElementById('q'), count = document.getElementById('count');
let current = null;
const esc = s => s.replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function card(s) {
  const doc = s.doc ? `<div class="doc">${esc(s.doc)}</div>` : '<div class="doc nodoc">No doc comment.</div>';
  return `<div class="card${s.parent ? ' member' : ''}"><span class="name">${esc(s.name)}</span><span class="kind">${s.kind}</span>`
    + `<pre>${esc(s.declaration)}</pre>${doc}<div class="where"><a href="${esc(s.link)}">${esc(s.path)}:${s.line}</a></div></div>`;
}
function show(lib, header) {
  current = [lib, header];
  const syms = DATA.libraries[lib].headers[header];
  main.innerHTML = `<h1>${esc(header)}</h1><p class="where">${esc(lib)} · ${syms.length} symbols</p>` + syms.map(card).join('');
  document.querySelectorAll('nav a').forEach(a => a.classList.toggle('on', a.dataset.lib === lib && a.dataset.header === header));
  main.scrollTop = 0;
}
function search(term) {
  const t = term.toLowerCase(), hits = [];
  for (const [lib, l] of Object.entries(DATA.libraries))
    for (const syms of Object.values(l.headers))
      for (const s of syms)
        if ((s.name + ' ' + s.declaration + ' ' + s.doc).toLowerCase().includes(t)) hits.push(s);
  main.innerHTML = `<h1>${hits.length} matches for “${esc(term)}”</h1>` + hits.slice(0, 400).map(card).join('');
}
for (const [lib, l] of Object.entries(DATA.libraries)) {
  const h = document.createElement('h2'); h.textContent = `${lib} (${l.symbols})`; tree.appendChild(h);
  const box = document.createElement('div'); tree.appendChild(box);
  h.onclick = () => box.hidden = !box.hidden;
  for (const header of Object.keys(l.headers)) {
    const a = document.createElement('a'); a.href = '#'; a.textContent = header;
    a.dataset.lib = lib; a.dataset.header = header;
    a.onclick = e => { e.preventDefault(); q.value = ''; show(lib, header); };
    box.appendChild(a);
  }
}
count.textContent = `${DATA.total} symbols · generated ${DATA.generated}`;
q.oninput = () => q.value.trim() ? search(q.value.trim()) : current && show(...current);
const first = Object.entries(DATA.libraries)[0];
if (first) show(first[0], Object.keys(first[1].headers)[0]);
</script>
</body>
</html>
"""


def render_html(catalog, root, html_dir):
    data = {"libraries": {}, "total": 0, "generated": time.strftime("%Y-%m-%d %H:%M")}
    for name, symbols in sorted(catalog.items()):
        headers = {}
        for header, group in sorted(_by_header(symbols).items()):
            headers[header] = [{
                "name": s["name"], "kind": s["kind"], "declaration": s["declaration"], "doc": s["doc"],
                "parent": s["parent"], "path": _repo_relative(s["path"], root), "line": s["line"],
                "link": _repo_relative(s["path"], html_dir),
            } for s in group]
        data["libraries"][name] = {"symbols": len(symbols), "headers": headers}
        data["total"] += len(symbols)
    payload = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    return _HTML.replace("__DATA__", payload)


# --- Driver ---------------------------------------------------------------------

def resolve_libclang(entries, log=print):
    """The libclang file to parse `entries` with, or None for the wheel's bundled one (warned)."""
    library_file = find_libclang(parse_flags(entries[0])[0])
    if library_file is None:
        log("warning: no libclang found beside the build's compiler or in a known LLVM install; "
            "using the pip wheel's, whose builtin headers may not match this standard library. "
            "Set BERNINI_LIBCLANG to a toolchain's libclang to silence the parse errors that follow.")
    return library_file


def generate(libraries, compile_db, out_dir, root=ct.REPO_ROOT, log=print, only=None, previous=()):
    """Parse the libraries named in `only` (default: all) and write the catalog into `out_dir`.

    A library not parsed is rendered from the symbols its last parse cached under `out_dir`, since
    a library's entries depend on its own headers alone. `previous` names the libraries the last
    run wrote, so a library since removed loses its file; nothing else in `out_dir` is touched.

    Returns (summary, parsed): {name: counts} for every library rendered, and the set actually
    parsed this run -- which is what a refresh may stamp as current.
    """
    stale = sorted(libraries) if only is None else sorted(only)
    catalog, parsed = {}, set()
    if stale:
        entries = load_compile_db(compile_db)
        if not entries:
            raise CatalogError(f"{compile_db} is empty.")
        cindex = load_cindex(resolve_libclang(entries, log))

        for name in stale:
            include_dir = libraries[name]
            entry = entry_for(entries, include_dir)
            if entry is None:
                log(f"warning: {name}: no translation unit in {compile_db} names {include_dir}; skipped.")
                continue
            _, flags = parse_flags(entry)
            symbols, errors = extract_library(cindex, include_dir, flags)
            if errors:
                log(f"warning: {name}: {len(errors)} parse error(s), so its catalog may be incomplete; "
                    f"first: {errors[0]}")
            catalog[name] = {"symbols": symbols, "errors": len(errors)}
            parsed.add(name)
            _write_cache(out_dir, name, catalog[name])

    for name in libraries:
        if name not in catalog:
            cached = _read_cache(out_dir, name)
            if cached is not None:
                catalog[name] = cached

    summary = {name: {"symbols": len(c["symbols"]), "documented": sum(1 for s in c["symbols"] if s["doc"]),
                      "errors": c["errors"]} for name, c in catalog.items()}
    html_dir = os.path.join(out_dir, "html")
    os.makedirs(html_dir, exist_ok=True)
    for name in set(previous) - set(catalog):
        for gone in (os.path.join(out_dir, f"{name}.md"), _cache_path(out_dir, name)):
            if os.path.isfile(gone):
                os.remove(gone)
    for name, c in catalog.items():
        _write(os.path.join(out_dir, f"{name}.md"),
               render_library_markdown(name, libraries[name], c["symbols"], root))
    _write(os.path.join(out_dir, "INDEX.md"), render_index_markdown(summary, root))
    _write(os.path.join(html_dir, "index.html"),
           render_html({name: c["symbols"] for name, c in catalog.items()}, root, html_dir))
    return summary, parsed


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def _cache_path(out_dir, name):
    return os.path.join(out_dir, CACHE, f"{name}.json")


def _write_cache(out_dir, name, parsed):
    os.makedirs(os.path.join(out_dir, CACHE), exist_ok=True)
    _write(_cache_path(out_dir, name), json.dumps(parsed))


def _read_cache(out_dir, name):
    try:
        with open(_cache_path(out_dir, name), encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def read_stamps(out_dir):
    try:
        with open(os.path.join(out_dir, STAMP), encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def refresh(libraries, compile_db, out_dir, root=ct.REPO_ROOT, force=False, log=print):
    """Re-parse each library whose public headers changed since the last run, and re-render.

    Returns the summary, or None when the catalog was already current. A new script, or a
    different libclang than the last parse used, re-parses everything: either can change what any
    line says. A library that could not be parsed keeps its old digest, so the next run tries again.
    """
    entries = load_compile_db(compile_db)
    if not entries:
        raise CatalogError(f"{compile_db} is empty.")
    stamps = read_stamps(out_dir)
    known = stamps.get("libraries", {})
    identity = {"script": script_digest(), "libclang": find_libclang(parse_flags(entries[0])[0]) or "bundled"}
    digests = {name: library_digest(include) for name, include in libraries.items()}
    if force or any(stamps.get(key) != value for key, value in identity.items()):
        stale = set(libraries)
    else:
        stale = {name for name, digest in digests.items()
                 if known.get(name) != digest or _read_cache(out_dir, name) is None}
    removed = set(known) - set(libraries)
    if not stale and not removed and os.path.isfile(os.path.join(out_dir, "INDEX.md")):
        return None

    summary, parsed = generate(libraries, compile_db, out_dir, root, log, only=stale, previous=known)
    current = {name: digests[name] if name in parsed else known[name]
               for name in summary if name in parsed or name in known}
    _write(os.path.join(out_dir, STAMP), json.dumps({**identity, "libraries": current}, indent=1) + "\n")
    return summary


def default_compile_db(preset=None):
    import util.config as cfg

    binary_dir = ct.binary_dir_of(cfg.preset(preset))
    return os.path.join(binary_dir, "compile_commands.json") if binary_dir else None


def _parse_libraries(values, root):
    libraries = {}
    for value in values:
        name, sep, path = value.partition("=")
        if not sep or not name or not path:
            raise CatalogError(f"--library takes NAME=DIR, not '{value}'.")
        libraries[name] = os.path.normpath(path if os.path.isabs(path) else os.path.join(root, path))
    return libraries


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="Regenerate even if no header changed.")
    parser.add_argument("--preset", help="Preset whose compile_commands.json supplies the flags "
                                         "(default: config.json's).")
    parser.add_argument("--compile-db", help="compile_commands.json to take the flags from (overrides --preset).")
    parser.add_argument("--library", action="append", default=[], metavar="NAME=DIR",
                        help="A library and its public include dir (default: every libs/*/include).")
    parser.add_argument("--root", default=ct.REPO_ROOT, help="Paths in the catalog are relative to this.")
    parser.add_argument("--out", default=DEFAULT_OUT, help="Output directory (default: build/api).")
    parser.add_argument("--quiet", action="store_true",
                        help="Say nothing when the catalog is current, and put an error as a note: "
                             "what a build runs it with.")
    args = parser.parse_args(argv)

    try:
        libraries = _parse_libraries(args.library, args.root) if args.library else default_libraries(args.root)
        compile_db = args.compile_db or default_compile_db(args.preset)
        started = time.monotonic()
        summary = refresh(libraries, compile_db, args.out, args.root, force=args.force)
    except CatalogError as err:
        print(f"{'note: API catalog not refreshed' if args.quiet else 'error'}: {err}", file=sys.stderr)
        return 1

    out = os.path.relpath(args.out)
    if summary is None:
        if not args.quiet:
            print(f"API catalog is current: {out}/INDEX.md")
        return 0
    total = sum(s["symbols"] for s in summary.values())
    documented = sum(s["documented"] for s in summary.values())
    print(f"API catalog: {total} symbols ({documented} documented) in {len(summary)} libraries, "
          f"{time.monotonic() - started:.1f} s -> {out}/INDEX.md, {out}/html/index.html")
    return 0


if __name__ == "__main__":
    sys.exit(main())
