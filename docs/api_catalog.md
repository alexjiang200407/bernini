# API catalog — every public symbol, generated

`build/api/` lists everything the libraries' public headers declare, one line per symbol, so a
helper can be found by what it does before a second one is written. It is generated from the
headers by [`scripts/api.py`](scripts/api.py) and never committed; the header at each `path:line`
is the source of truth.

| File | For | Holds |
|---|---|---|
| `build/api/INDEX.md` | agents | the libraries, their counts, how to search |
| `build/api/<library>.md` | agents | a line per symbol, grouped by header: qualified name, kind, declaration, the first sentence of its doc comment, `path:line` |
| `build/api/html/index.html` | people | the same symbols to browse by library and header, with the full doc comment and a search box |

## Searching it

Grep it by **behaviour**, in whole words, and read every hit: `grep -iwE 'ceil|round up'
build/api/*.md`. Three habits decide whether it finds anything, each one measured to have missed
a helper that was there:

- **Whole words.** `ring` matches every `string` in `core.md`, and the line wanted scrolls past.
- **Every hit, not `| head`.** A common word (`reference` in `assetlib.md`) fills the first fifty
  lines before the other file's hit is reached.
- **As well as the code, never instead of it.** A symbol with no doc comment is listed by its
  declaration alone — `core::str::split_once` carries no sentence for a search by behaviour to
  match — so a miss here proves nothing. clangd and grep still answer what the catalog cannot.

The catalog makes a search cheaper; it does not make one happen. An idiom written without
thinking — `throw std::runtime_error(std::format(...))` where `core::throw_runtime_error` is the
rule — is never looked up, and the precheck's `core` table in [`ws-profile.md`](ws-profile.md) is
what catches it.

## How it is generated

- **Every build refreshes it** once the build succeeds -- `just build`, and the build step of
  `just run`, `test`, `coverage` and `idl`, which all go through `scripts/build.py` -- in a process
  of its own, so a crash in libclang cannot fail a good build. A library is re-parsed only when
  one of its public headers changed (a digest per library, in `build/api/.stamp.json`); the others
  render from the symbols their last parse cached under `build/api/.symbols/`. `--no-api` skips
  it, and `just api` runs it alone (`--force` re-parses everything).
- **libclang parses it**, one translation unit per library including every public header, with
  function bodies skipped and the flags `compile_commands.json` records for that library. So it
  needs a Ninja preset's configured build dir, as clangd and `just tidy` do. A library with no
  `src/` of its own borrows the translation unit that names its include dir and sees the most.
- **The libclang is the toolchain's own** — beside the compiler the database names, then a known
  LLVM install — because its builtin headers must match the standard library the flags name. The
  pinned `libclang` wheel supplies the Python bindings; its bundled library is the fallback, and
  trips over a newer libc++. `libclang` in `scripts/config.json` overrides the choice.
- **It never fails a build.** No bindings, no compile database, a parse error: one line says so,
  and the last catalog stays.

## What counts as public

Everything a header under `libs/*/include/` declares at namespace scope, and the public members
of the classes it defines -- methods, nested types, data members and static constants, since a
descriptor's defaults and units are documented on its fields -- except:

- a namespace named `detail`, `details`, `impl` or `internal`;
- a forward declaration, an operator, a deleted member, a copy or move constructor.

Internal headers under `src/`, `apps/`, shaders and Python are not listed.

## Another tree

Nothing in it names this repository: `--library NAME=DIR` (repeatable) replaces the
`libs/*/include` discovery, `--compile-db` names the database, `--root` what paths are relative to
and `--out` where it writes. A game catalogues its own sources with the same script.
