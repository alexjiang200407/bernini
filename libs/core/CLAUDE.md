
# core

core is basically a static library that contains shared utilities and containers for all other targets.

State that must exist once per process does not go in it: that is `core_process`, compiled from
`src/process/` and marked `CORE_PROCESS_API`. See [docs/core_process.md](../../docs/core_process.md).