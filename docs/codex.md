# Claude and Codex

Both agents read `CLAUDE.md`. `just init` creates the relative `AGENTS.md -> CLAUDE.md` symlink in
this repository, including when an existing machine config is kept, and
`just init --agents-only` repairs it without installing tools or changing build configuration. An
existing regular `AGENTS.md` is kept with a warning; a stale symlink is repaired. Windows needs
Developer Mode, or permission to create symlinks.

That link is the whole of the Codex integration bernini carries. The agent flows, their hooks, the
PR tooling and the Codex adapter that mapped them onto Codex's hooks, MCP tools and agent roles live
in the bernini-workspace repo, beside the Claude versions they adapt; bernini keeps only
[`.bcp/profile.md`](../.bcp/profile.md), which either agent reads as plain Markdown. See
[ai-coding.md](ai-coding.md).

A Codex session in a plain clone therefore has the map and the profile and no flows: read the code
with clangd where it is installed, and use `scripts/bgrep` — plain grep under a name that says the
search was meant — for text, Slang identifiers and completeness sweeps.

What an older `just init` generated under `.codex/` and `.agents/` is no longer read, and can be
deleted.
