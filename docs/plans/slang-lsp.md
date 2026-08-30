# slang-lsp — implementation plan

## Context

125 `.slang` sources sit under `libs/bgl/shaders/src` and `libs/bgl/idl/src`, spread across `idl/`,
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
and `libs/bgl/shaders/CMakeLists.txt` already runs `slangc` over all 25 entry points at build time,
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

## Non-goals

- No MCP server, no `.mcp.json`, no third-party bridge.
- No diagnostics, and no new `just` recipe or `scripts/` entry.
- No second language in the plugin — `clangd` for the C++ is a separate decision, and Anthropic
  already ships that one.
- No change to the build, to CI, to `vcpkg.json`, or to the committed `.claude/settings.json`.
- Not required: a developer who never installs the plugin sees no change, and nothing fails without
  `slangd` on `PATH`.

## Acceptance

- `claude plugin validate --strict <plugin path>` passes.
- With the plugin installed and `slangd` on `PATH`, an `LSP` `goToDefinition` on `SubmeshInstance` at
  `libs/bgl/shaders/src/forward/common.slang:1` resolves into
  `libs/bgl/shaders/src/types/SubmeshInstance.slang` — a cross-file module import, which is the thing
  grep does worst and the only reason to do this at all.

## Commits

1. `docs(plans): plan the slangd LSP plugin` — this file. Gate: none; it is the boundary the rest is
   read against.
2. `feat(tooling): a repo-local plugin points Claude Code at slangd` — the marketplace entry, the
   plugin, and the README's Soft Requirements entry. Gate: `claude plugin validate --strict` on the
   plugin, and an `LSP` `goToDefinition` resolving `SubmeshInstance` across two `.slang` files.
