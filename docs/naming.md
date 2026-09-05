# Naming

Bernini writes names in two styles, and which one you use is decided by **where the file lives**,
not by what the thing feels like. That is the whole rule; everything below is detail.

| Directory | Style | Why |
|---|---|---|
| `libs/core/include/core/containers/`, `libs/core/include/core/str/` | `lower_case` throughout | These substitute for standard-library types. `static_vector` has to be usable where a `std::vector` was, down to `value_type` and `push_back`. |
| the rest of `libs/core/` | `lower_case` free functions, `PascalCase` types and methods | A `core` helper is read beside `std::` ones in the same expression — `split_once(str, "/")`, not `SplitOnce`. |
| `libs/assetlib/`, `libs/assetlib_structs/` | `camelBack` free functions, `PascalCase` types and methods | The codec surface has always spelled them this way -- `loadKTX2`, `bake`, `serialize` -- and a caller reads `loadKTX2(path)` beside `load(path)`. `assetlib_structs` holds the PODs it decodes into, and `toMatrix(node.localTransform)` is read in the same expression as the rest. |
| `libs/assetlib/tests/` | `PascalCase`, as everywhere else | A fixture is engine code. Only the published codec functions read beside `std::`. |
| everything else | `PascalCase` types and functions | Engine code. `bgl`, `bgl_extended`, `gamelib`, `apps`, `examples`. |

The boundary is a directory because a directory is checkable and a judgement call is not. The
earlier form of this rule — "snake_case for containers, camelCase *or* snake_case for utilities" —
asked the author to classify what they were writing, and the answer came out differently on
different days: `core::str` held `split_once` next to `toUtf32`, and `core::file` had
`readFileBytes` where `core::err` had `install_crash_handlers`. Moving a name into the std-shaped
world is now a deliberate act: you put the file under `containers/` or `str/`, or you add a
`.clang-tidy` beside it.

## The rules, per identifier

Set out in [STYLE.md](../STYLE.md) and encoded in [`.clang-tidy`](../.clang-tidy):

| Kind | Style |
|---|---|
| namespace | `lower_case` |
| class, struct, enum, alias, concept | `PascalCase` |
| enum constant | `kPascalCase` |
| function, method | `PascalCase` |
| private / protected member | `m_PascalCase` |
| public (struct) member | `camelBack` |
| parameter, local | `camelBack` |
| global / static variable | `g_PascalCase` |
| constant, constexpr | `c_PascalCase` |
| macro | `UPPER_CASE` |

## Checking it

```bash
just tidy                              # every source file the configured preset compiles
just tidy libs/core                    # one subtree
just tidy --fix                        # apply the renames and the includes clang-tidy suggests
just tidy --changed                    # staged lines only -- what the pre-commit hook runs
just tidy --changed origin/master      # the lines this branch changed
```

`just tidy` needs a build directory holding `compile_commands.json`, so a **Ninja or Makefile
generator** — the Visual Studio generator does not write one. On Windows that means configuring a
Ninja preset once:

```bash
just build --preset windows-clang-dx12-debug
```

clang-tidy itself is found the same way clang-format is: `tools.clang-tidy` in `scripts/config.json`
first, then `PATH`, then a versioned `clang-tidy-<n>` on `PATH` (how Debian and Ubuntu ship it), then
a known LLVM install — the Visual Studio LLVM component or `C:\Program Files\LLVM` on Windows,
Homebrew's keg-only `llvm` on macOS, `/usr/lib/llvm-*/` on Linux. Finding none, `just init` offers
the version pinned in [`scripts/requirements.txt`](../scripts/requirements.txt) as a wheel, which
needs no LLVM install; `brew install llvm`, `apt install clang-tidy` and the "C++ Clang tools for
Windows" component all work too.

## The other check: includes

`just tidy` runs one more check, and it is not about names. `misc-include-cleaner` reports a symbol
used without including the header that declares it, and an include that nothing in the file uses --
the rule in [CLAUDE.md](../CLAUDE.md) that a PCH is an optimisation and never an interface. The why
is in [docs/build_performance.md](build_performance.md); what follows is how it is switched on.

**A subsystem opts itself in.** The root config enables it nowhere; a subsystem adds it to its own
`Checks`, which append under `InheritParentConfig`:

```yaml
InheritParentConfig: true

Checks: >
  misc-include-cleaner
```

That is what let the tree be swept one subsystem at a time without leaving the rest ungated
meanwhile, and it is why `just tidy` can be silent about includes in one directory and strict in the
next. The set of subsystems carrying that line is the record of how far the sweep reached.

Both checks fail the file, so the pre-commit hook refuses either -- but `--changed` filters findings
to the lines a diff touched, so an existing violation elsewhere in a file is not the problem of
whoever edits it next.

`just tidy --fix` writes the missing includes and removes the unused ones, then rebrackets them:
`misc-include-cleaner` quotes everything but the standard library, and
[`scripts/util/include_style.py`](../scripts/util/include_style.py) turns `"bgl/IScene.h"` into
`<bgl/IScene.h>` while leaving a subsystem's own `src/` header quoted. **Run `just format`
afterwards** -- the bracket decides the sort order.

Review the removals; they are the half no compiler checks. An include held for a side effect rather
than a symbol looks unused: a subsystem `pch.h` entry is unused where it sits by construction and
takes `// IWYU pragma: keep`. A header that exists to *re-export* another takes
`// IWYU pragma: export` instead, which is the stronger claim -- `libs/core/include/core/glm.h`
carries it, so `<core/glm.h>` counts as the header providing `glm::vec3` rather than merely being
tolerated. Where even that is not enough, the header is excluded outright: an export reaches only
what a header includes directly, so glm is additionally named in
`misc-include-cleaner.IgnoreHeaders` and `.clangd`'s `IgnoreHeader` to stop a vendor `detail/`
header being offered in `<core/glm.h>`'s place. Both take the same pattern only because it is
written to full-match -- clangd anchors, clang-tidy searches.

The same library backs clangd, configured in [`.clangd`](../.clangd), so the editor underlines what
the hook would refuse.

### What the tool cannot see

`misc-include-cleaner` reasons about *symbol references*. Everything below is a real dependency that
is not one, so the check reports the header unused and deleting it breaks something. Each was found
by building, not by reading, and each already has its answer in the tree — the point of the list is
that the next one you meet is probably on it.

| The dependency | Where it bit | The answer |
|---|---|---|
| A header that must be **configured** before it is parsed | `core/glm.h` sets `GLM_FORCE_DEPTH_ZERO_TO_ONE`; glm reached any other way builds a projection matrix for the wrong depth range | excluded, with its entry points |
| A header that **does not exist off-platform** | `<sys/_types/_ucontext.h>` is Darwin-internal; `<mach/arm/…>` is not even on an Intel mac | excluded |
| A header that is **not self-contained** | `<RmlUi/Core/Span.h>` uses `RMLUI_ASSERT` and includes nothing that defines it | excluded; the umbrella is the entry point |
| An **umbrella that is the interface** | `metal_cpp.h` in `MetalImpl.cpp` emits metal-cpp's out-of-line symbols — deleting it compiles and breaks the *link* | excluded |
| **Declared** in one header, **defined** in another | `nlohmann::json` is declared in `json_fwd.hpp`, defined in `json.hpp`, and the check names the declaration | excluded |
| A **namespace alias** | `logger = spdlog` in `bgl_common/gassert.h`; using `logger::warn` references no symbol from it | `// IWYU pragma: keep` |
| A type needed **complete inside a template** | `SharedRef<T>` dereferences and destroys T, `unique_ptr` needs `sizeof` for its deleter, `optional<T>` needs the definition | `// IWYU pragma: keep` |
| A **template specialisation** | `AssetCodec<BMesh>` in `codecs.h` satisfies `SaveAt`'s concept; the call simply stops matching | `// IWYU pragma: keep` |

The last two have no textual signal at all. `auto q = device->CreateCommandQueue(...)` needs
`ICommandQueue` complete while naming neither the type nor the header nor even `SharedRef`, so no
heuristic finds it and only a compiler does — and only for the configuration it just built. That is
why the *unused* half is reviewed and the *missing* half is not, and why a sweep is not finished
until CI's Windows compile is green as well as the local one.

### Where the check is deliberately off

`just tidy` prints a count of the files it skipped, and a subsystem the sweep could not reach says so
in its own `.clang-tidy` rather than reporting clean over code nothing examined:

| Not checked | Why | What lifts it |
|---|---|---|
| `libs/bgl_extended` (removals only) | its interfaces are `core::SharedRef<T>` behind `auto` — the row above | a human pass per file |
| `libs/bgl_extended/src/d3d12`, `libs/core/src/win32`, `libs/core/src/web` | the macOS preset has no compile command for them | `just build --preset windows-clang-dx12-debug`, then `just tidy <dir>` |
| `bgl_extended/src/cmd/Command{Allocator,List,Queue}.h`, `src/constants/constants.h`, `src/pch.h`, `gamelib/tests/src/util/RigFixture.h` | their directories hold no compiled `.cpp` for `tidy.py` to borrow flags from | a compiled source beside them |

## Where the config lives

| File | Says |
|---|---|
| [`.clang-tidy`](../.clang-tidy) | The engine's rules. `readability-identifier-naming` everywhere; `misc-include-cleaner` nowhere, until a subsystem asks for it. |
| [`libs/core/.clang-tidy`](../libs/core/.clang-tidy) | `lower_case` free functions, and `lower_case` concepts where they stand in for `<type_traits>`. |
| [`libs/core/include/core/containers/.clang-tidy`](../libs/core/include/core/containers/.clang-tidy) | `lower_case` everything. |
| [`libs/core/include/core/str/.clang-tidy`](../libs/core/include/core/str/.clang-tidy) | `lower_case` everything. |
| [`libs/core/tests/.clang-tidy`](../libs/core/tests/.clang-tidy) | `PascalCase` functions again: a fixture is engine code. |
| [`libs/assetlib/.clang-tidy`](../libs/assetlib/.clang-tidy) | `camelBack` free functions. |
| [`libs/assetlib_structs/.clang-tidy`](../libs/assetlib_structs/.clang-tidy) | `camelBack` free functions, like the codec that decodes into them. |
| [`libs/assetlib/tests/.clang-tidy`](../libs/assetlib/tests/.clang-tidy) | `PascalCase` functions again. |
| [`libs/gamelib/.clang-tidy`](../libs/gamelib/.clang-tidy) | The names RmlUi and Lua own: the `Rml` namespace, the `lua_State` typedef. |
| [`examples/util/.clang-tidy`](../examples/util/.clang-tidy) | The one name RmlUi owns, again: the `Rml` namespace its SDL translation forward-declares. |
| [`apps/editor/.clang-tidy`](../apps/editor/.clang-tidy) | The names Qt and Catch2 own: virtual hooks, `StringMaker::convert`, the `QtNodes` namespace. |

Each of the narrowing files sets `InheritParentConfig: true`. Without it a child config *replaces*
the parent rather than merging with it, and the omitted rules silently stop being checked.

## Two things about how this runs

**Every file is checked as its own translation unit, headers included.** clang-tidy reads a check's
options once per TU, from the `.clang-tidy` beside the *main* file — so a header pulled into another
subsystem's TU is judged by whichever config that TU sits under. Checking headers directly is what
makes the nearest config the deciding one, and it is also the only way header-only code gets checked
at all: nothing in `core/containers/` has a `.cpp`. `HeaderFilterRegex` is empty everywhere for the
same reason — a file is diagnosed when it is the main file, and only then, so nothing is reported
twice.

**The compile database is rewritten before use**, into `<build>/clang-tidy/`. A binary PCH can only
be read by the clang that wrote it, and clang-tidy is rarely that clang — Apple ships no clang-tidy,
so on macOS it comes from Homebrew's LLVM while the build uses Xcode's, and the mismatch is a fatal
`PCH file uses an older format`. `scripts/tidy.py` strips `-include-pch` (and MSVC's `/Yu`) from
every entry, leaving the textual `-include` of the same header. That force-include stays because it
is what the build compiles with, and a check judging a translation unit the compiler never sees is
judging the wrong thing — and until every subsystem has been swept it is also the only reason a
source that leans on the PCH still parses.

That force-include points at a copy of CMake's `cmake_pch.hxx` with its
`#pragma clang system_header` removed. The pragma makes everything the PCH pulls in a system header,
and clang-tidy says nothing about those — so a header the PCH includes would come back clean however
it was named. The copy holds absolute includes, so it parses the same from `<build>/clang-tidy/`.

## What it covers, and what it doesn't

A sweep only reaches what the configured preset compiles, and no single preset compiles everything.
On `macos-clang-metal-debug` the D3D12 backend has no compile commands and is skipped, which the run
says out loud — with a count — rather than counting it as a pass. Checking a Windows preset as well
as a macOS one is what covers the tree.

A header its own target's PCH pre-includes cannot be the main file: `#pragma once` only suppresses
an `#include`, and the main file is not one, so every declaration in it is a redefinition and the
parse dies before a name is looked at. `bgl/error.h`, `bgl/src/uniforms/DescriptorHandle.h` and
`bgl/src/metal/MetalErrorChecker.h` are the ones today. Those are checked through the source they
borrowed their flags from — the translation unit they are really compiled in — filtered back down to
themselves, which is why the compile database also carries an entry for the lender.

`--fix` renames mechanically and its output wants reading. It rewrites to the rule, not to intent:
a static `s_Live` that should be `g_Live` becomes `g_SLive`, because the check has no idea `s_` was
a prefix rather than the start of the name.

## Adding a std-shaped corner

Put the files in their own directory and drop a `.clang-tidy` beside them:

```yaml
InheritParentConfig: true

CheckOptions:
  readability-identifier-naming.StructCase: lower_case
  readability-identifier-naming.MethodCase: lower_case
  readability-identifier-naming.FunctionCase: lower_case
```

The bar for doing this is not "it feels like library code". It is that the type stands in for a
standard-library one, so a caller can swap it for the `std::` type — or back — without touching the
call sites.
