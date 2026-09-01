# slang-lsp — implementation plan

## Context

125 `.slang` sources sit under `libs/bgl_extended/shaders/src` and `libs/bgl_extended/idl/src`, spread across `idl/`,
`types/`, `forward/`, `util/` and `debug/`, and every cross-file reference is a Slang module import
(`import types.SubmeshInstance`). An agent working in that tree has grep and nothing else: no
go-to-definition, no find-references, no hover on a declaration. The ask was whether a "slang-mcp"
exists that would fix this.

It does not. The `shader-slang` organisation has no MCP server, and its only agent tooling —
`shader-slang/slang-skills`, experimental — is for hacking on the Slang *compiler*, not for authoring
shaders in a downstream engine. What NVIDIA does ship is `slangd`, the Slang language server behind
the official VS Code and Visual Studio extensions, and it is already built here: the vcpkg
`shader-slang` port this repo declares (`vcpkg.json:8`) installs it beside `slangc` at
`build/<preset>/vcpkg_installed/<triplet>/tools/shader-slang/slangd`.

Nothing is known to be broken today. No shader bug has been traced to an agent misreading the tree,
and `libs/bgl_extended/shaders/CMakeLists.txt` already runs `slangc` over all 25 entry points at build time,
so a shader that does not compile is a build failure. This is a speculative convenience, and it is
scoped as one.

## Decisions

- **ADR-1 — Consume `slangd` over LSP, not through an MCP server.** The language server is the
  standard way a tool understands a shader codebase: NVIDIA ships `slangd` and every editor
  integration consumes it over LSP. Claude Code's own standard consumption path is the same, and its
  `LSP` tool already exists. *Rejected: a generic MCP↔LSP bridge (`isaacphi/mcp-language-server`,
  `rockerBOO/mcp-lsp-bridge`), because it buys only the diagnostics operation the built-in tool
  lacks, at the price of an unaudited third-party binary, a server process per session, and the
  repo's first agent-only path that CI cannot run.*

- **ADR-2 — Ship the config as a repo-local plugin, installed on purpose.** Claude Code has no
  project-root `.lsp.json`; verified against the installed binary, every reference to it is
  plugin-scoped ("Path to .lsp.json configuration file relative to plugin root"). The native
  mechanism is `lspServers` in a plugin manifest, which is how Anthropic's own `clangd-lsp` is
  built — and none of its 13 official LSP plugins covers Slang. So the repo carries a marketplace
  with one plugin and the README says how to add it. *Rejected: registering it from the committed
  `.claude/settings.json` via `extraKnownMarketplaces`/`enabledPlugins`, because that turns it on
  for every developer and every agent without asking, which is not what "optional" means.*

- **ADR-3 — `slangd` is found on `PATH`, and the README says where it came from.** A plugin
  manifest's `command` is resolved on `PATH` or given absolutely; an absolute path cannot be
  committed, because `slangd` lives under a build directory whose preset and triplet differ per
  machine. *Rejected: teaching `scripts/init.py` or `ws doctor` to stage or symlink `slangd`,
  because a speculative convenience does not earn a change to the machine-setup path every clone
  runs.*

- **ADR-4 — No diagnostics.** The `LSP` tool exposes hover, definition, references, symbols and call
  hierarchy, and has no diagnostics operation, so this delivers navigation only. A shader that does
  not compile is still caught where it is caught today: `just build`. *Rejected: a
  `just check-shaders` recipe running `slangc` over the tree, because it answers a question the
  build already answers and no measured latency complaint asked for it.*

- **ADR-5 — Amends ADR-4: diagnostics do reach you, and that is kept.** ADR-4's decision stands
  — nothing was built to produce them — but its premise was wrong. "No diagnostics operation" is
  true
  of the `LSP` tool's callable operations and says nothing about `publishDiagnostics`, which slangd
  pushes unasked and Claude Code surfaces; the plugin's `diagnostics` toggle defaults to on. So a
  shader mistake can reach a session before a build does, which the Non-goal below denied. Observed,
  not designed: these diagnostics are what exposed the missing search path that ADR-6 fixes.
  *Rejected: setting `diagnostics: false` to make the Non-goal true again, because the Non-goal was
  refusing to build a checker, not refusing to be told.*

- **ADR-6 — Name the source root the build names.** slangd takes no include flags and honours only
  what it pulls over `workspace/configuration`, which Claude Code answers from the plugin's
  `settings`; declaring none left every section null, and a null
  `searchInAllWorkspaceDirectories` reads as false. `compile_shader` passes `SLANG_SOURCE_ROOT`, so
  that one path is what the compiler resolves against and what slangd gets. It stays relative to
  remain machine-independent, which is safe because slangd resolves it against its cwd and Claude
  Code launches the server in the same directory it passes as `rootUri`. *Rejected:
  `searchInAllWorkspaceDirectories`, which also fixes it but walks `build/` — gigabytes of
  regenerated output holding vcpkg's own copy of the Slang core module. Also rejected: a second root
  for `libs/bgl_extended/idl/src`, measured and found to be a no-op, since those imports are same-directory
  siblings that Slang resolves beside the importing file.*

## Non-goals

- No MCP server, no `.mcp.json`, no third-party bridge.
- No diagnostics *built* — no new `just` recipe or `scripts/` entry. Diagnostics nonetheless
  surface, which ADR-5 records.
- No second language in the plugin — `clangd` for the C++ is a separate decision, and Anthropic
  already ships that one.
- No change to the build, to CI, to `vcpkg.json`, or to the committed `.claude/settings.json`.
- Not required: a developer who never installs the plugin sees no change, and nothing fails without
  `slangd` on `PATH`.

## Acceptance

- `claude plugin validate --strict <plugin path>` passes.
- With the plugin installed and `slangd` on `PATH`, an `LSP` `goToDefinition` on `SubmeshInstance` at
  `libs/bgl_extended/shaders/src/forward/common.slang:1` resolves into
  `libs/bgl_extended/shaders/src/types/SubmeshInstance.slang` — a cross-file module import, which is the thing
  grep does worst and the only reason to do this at all.
- **Insufficient, as it turned out.** slangd answers that query from its file search rather than the
  compiler, so it passes whether or not the module resolves — and it did pass while nothing else
  worked (ADR-6). The check that bites is `hover` on `SubmeshInstance` at `common.slang:6`, which
  has to resolve the import to answer at all.

## Commits

1. `docs(plans): plan the slangd LSP plugin` — this file. Gate: none; it is the boundary the rest is
   read against.
2. `feat(tooling): a repo-local plugin points Claude Code at slangd` — the marketplace entry, the
   plugin, and the README's Soft Requirements entry. Gate: `claude plugin validate --strict` on the
   plugin, and an `LSP` `goToDefinition` resolving `SubmeshInstance` across two `.slang` files.
