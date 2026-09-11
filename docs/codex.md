# Claude and Codex

Both agents use `CLAUDE.md`. `just init` creates the relative `AGENTS.md -> CLAUDE.md`
symlink in this repository, including when an existing machine config is kept.
`just init --agents-only` repairs the link without installing tools or changing build
configuration. It also generates `.codex/config.toml` from the tracked template using the running
Python interpreter and absolute tool paths. Rerun init after moving the checkout.
User-owned Codex configs are preserved. `--show` writes nothing. Existing regular `AGENTS.md` files are preserved
with a warning; stale symlinks are repaired. Windows needs Developer Mode or permission
to create symlinks. Init repairs the plain-text `.agents/skills` placeholder produced
by Git with `core.symlinks=false`; skills remain shared through a directory symlink.

The workspace's `ws init` owns its separate instruction link. Feature and game setup
backfills their links. The relative links continue to work when a checkout moves.

## Setup and selection

Install and sign in to the chosen CLI. This integration uses Codex CLI's native hooks,
MCP, custom agent roles, skills, and `codex queue` (validated against 0.154.0).
On first opening a checkout, trust the project and review its hooks with `/hooks`.
Codex skips untrusted hooks; ordinary permission bypass does not enable them. These
scripts do not bypass hook trust or change your global trust settings.

Bernini supports native Windows and macOS. Run `python scripts/init.py --agents-only`
(or `just init --agents-only`) from PowerShell on Windows before opening Codex.
MCP runs Python directly; hook configuration uses the documented
[Windows command override](https://learn.chatgpt.com/docs/hooks).
Text files use UTF-8. MCP bgrep invokes the existing script through Git for Windows
Bash, and Slang discovery includes `slangd.exe`. Install Git for Windows and clangd;
no workspace scripts are needed. The separate bernini-workspace is Unix-only.

In a workspace:

```sh
ws init --codex                   # choose Codex as the machine default
ws init --claude                  # choose Claude
ws feature lighting "fix the flicker" --codex --one-shot
ws feature lighting --codex --continue
ws ask skinning "how does blending work?" --codex
ws cmd lighting --codex           # foreground Codex in the checkout
ws cmd mygame --codex             # includes the game's engine/build environment
```

`ws-config.json` holds `{"agent":"codex"}`. A missing file or missing `agent` key
means Claude. `ws init` prompts for the default at a terminal and preserves it in
scripted runs; `--default-agent claude|codex` also sets it explicitly. Per-command
`--codex` and `--claude` override the default. `ws cmd` still runs any explicit command
unchanged, so `ws cmd lighting -- codex` works too.

`--model` passes the selected agent's model ID through. Codex keeps its configured
model when none is given; Claude's Opus/Sonnet/Haiku menu is only shown for Claude.
Feature `--mode bypassPermissions` maps to Codex's full-access unattended flag;
`default` and `acceptEdits` map to workspace-write with on-request approval, and
`plan` maps to read-only. These are permission mappings, not a claim that the two
CLIs have identical planning interfaces. Ask sessions always use read-only/never.

`ws sessions`, `ws attach`, and `ws kill` work with either CLI. Continue uses the
selected CLI's history; it does not convert transcripts across agents. Ask topics
store Claude and Codex session IDs separately. A Codex SessionStart hook records
the actual thread ID, so topics in the same checkout resume independently.

Workspace launchers exclude personal Codex skills by name for that invocation,
leaving the project `bcp-*` skills available. `ws-codex-skills.json` can opt names
back in with `{"my-skill":true}`. Direct `codex` invocations use personal defaults.
No global skill settings are rewritten.

## Tools and workflows

`.agents/skills` links to `.claude/skills`: the ten `bcp-*` workflows have one source.
Invoke them as `$bcp-feature`, `$bcp-implement`, `$bcp-ask`, etc. The ws launchers use
that spelling automatically. Paths under `.claude/features` and shared draft paths
stay the same across agents; `.claude` here is a storage path, not a CLI dependency.

Use these Codex equivalents when a shared skill names Claude tools:

| Shared skill operation | Codex equivalent |
| --- | --- |
| Read / Glob | `bernini.read_file` / `bernini.list_files` MCP tools |
| LSP | `bernini.lsp`, with `goToDefinition`, `findReferences`, `hover`, `documentSymbol`, `workspaceSymbol`, `goToImplementation`, `incomingCalls`, or `outgoingCalls` |
| Explicit text search | `bernini.bgrep` or `scripts/bgrep` in a shell |
| Edit / Write | `apply_patch`; in an ask session, `bernini.write_spec` only |
| AskUserQuestion / selectable forks | `request_user_input_async` or `request_user_input`, subject to availability and active-mode restrictions; the rule lives in `CLAUDE.md` |
| Skill invocation | `$bcp-…`, or read and follow the linked SKILL.md |
| Agent `bcp-docmap` / `bcp-precheck` | The corresponding Codex agent role; use the configured Codex subagent model, not a Claude tier name |

The MCP server is `scripts/agent_tools.py`, using Python's standard library. It
keeps one clangd and one slangd process per client and synchronizes files before
navigation. Positions are **1-based UTF-16**. clangd needs this checkout's
`compile_commands.json`; `ws init` seeds the engine links, and a game with one
configured build database is discovered automatically. Missing servers and protocol
errors are reported as tool errors, not empty symbol results. Unsupported operations
are rejected using server capabilities; this slangd provides hover, definitions and
document symbols, but not references or call hierarchy. Slang uses the shader
search paths from this engine. Configure/build first for accurate semantic answers.

The Codex search guard refuses direct Grep and ordinary shell grep/rg content searches;
use bgrep for deliberate text or completeness searches. `rg --files` remains file
discovery. This is a workflow guard, not a security boundary against arbitrary shell
programs. The existing LSP and C++ sed-edit guard also remains active.

The Codex hook adapter reuses the PR write guard, automatic draft commits, and
unwatched-PR Stop guard. `ws ask` refuses shell and arbitrary editing tools and uses
the MCP file/navigation tools. Its only writing tool validates a Markdown destination
under `docs/specs`, including symlink resolution, then commits the shared draft.
The read-only sandbox is also enabled. Do not work around a blocked ask tool.

## Waiting for a PR in Codex

Codex's shell execution does not provide Claude's `run_in_background` wake-up
contract. Start the existing watcher as a background process with explicit delivery
back to the current Codex thread. In a Unix shell:

```sh
mkdir -p .claude/features
just watch-pr 123 --notify-codex >.claude/features/pr-123.watch.log 2>&1 &
```

In Windows PowerShell, start Python directly (the child inherits `CODEX_THREAD_ID`):

```powershell
New-Item -ItemType Directory -Force .claude/features | Out-Null
Start-Process -FilePath (Get-Command python).Source -ArgumentList @('scripts/watch_pr.py', '123', '--notify-codex') -RedirectStandardOutput .claude/features/pr-123.watch.log -RedirectStandardError .claude/features/pr-123.watch.err
```

The watcher retains its normal claim and single-watcher behavior. When activity
arrives it stores the full JSON event in the checkout's git directory and calls
`codex queue --thread "$CODEX_THREAD_ID" --message …`. The queued message names the
event file; read it and continue the shared workflow. If queuing fails, the event
and error remain on disk. `--once` never queues a message. Do not substitute a
foreground wait or periodic `gh` polling. ws's process watchdog cleans background
work up when the CLI exits. PR watchers explicitly register with it, so cleanup
also works when Codex's shared app-server starts them outside the CLI process tree.
This registry is Unix workspace behavior; standalone Windows watchers use Bernini's
existing platform-aware process claims.

## Games

A game remains its own repository. `ws game new/add/clone` seeds its instruction
link, project skills, and Codex configuration; `ws cmd <game> --codex` supplies the
engine path. Read the game's CLAUDE.md before any engine workflow: changes to the
engine belong in an engine feature checkout. Engine `just pr`, build, and feature
tracker commands resolve from the engine and are not game-repository commands.
Game names start with a letter and contain only letters, digits, `_`, and `-`;
`bernini` and `master` are reserved. This prevents filesystem and CMake/template
ambiguities before a repository is created.

Codex integration contracts: [hooks](https://learn.chatgpt.com/docs/hooks),
[configuration](https://developers.openai.com/codex/config-reference), and
[skills](https://developers.openai.com/codex/skills). Consult `codex --help` for the
installed CLI's flags.
