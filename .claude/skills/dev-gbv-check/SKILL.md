---
name: dev-gbv-check
description: Check origin/dev end to end on Windows/D3D12 - build it in a gitignored worktree, run every suite, run bgl_extended_tests under GPU-based validation one file at a time with a Monitor on errors only, fix what it finds, and open a bugfix PR against dev. Use when asked to check, verify or GBV-test the dev branch.
---

# Check origin/dev under GPU-based validation

The goal is a PR against `dev` that fixes every bug the build, the suites and a `--gpu-validation`
run of `bgl_extended_tests` turn up. Work in a worktree so the user's checkout is never touched.

## 1. Worktree

Worktrees live in `.worktrees/` at the repo root, which is gitignored.

```bash
root=$(git rev-parse --show-toplevel)
git -C "$root" fetch origin
stamp=$(date +%Y%m%d-%H%M)
wt="$root/.worktrees/dev-gbv-$stamp"
git -C "$root" worktree add -b "fix/dev-gbv-$stamp" "$wt" origin/dev
cp "$root/scripts/config.json" "$wt/scripts/config.json"   # machine settings; git-ignored, so not in a worktree
git -C "$wt" lfs pull
```

Before starting, see whether the bug is `dev`'s at all: `git rev-list --count origin/dev..origin/master`
and `origin/master..origin/dev`. When dev is master plus commits, every finding must be checked
against master (`git log -S"<code>" origin/master -- <file>`). A bug master already has belongs
in a PR to master, so tell the user which it is before opening anything.

## 2. Build

Run `just build` in `$wt` with `run_in_background`; it takes minutes on a cold ccache. Warnings are
errors under the strict flags. Fix what fails in the worktree, then continue. Never rebuild while a
suite or the sweep below is running from that `bin/` directory: the exe is replaced under it.

## 3. The other suites, plainly

```bash
just test --no-build assetlib bgl_common bgl_tests bgpu core crowdlib editor_plugin editor_tests gamelib scripts
```

Only `bgl_extended_tests` takes `--gpu-validation`. Check `just test --list` for new suites first
and add any the list above misses.

## 4. The GPU-validation sweep, with a Monitor on errors

```bash
bin=$(dirname "$(cd "$wt" && just exes --target bgl_extended_tests | tail -1)")
out="$wt/.gbv"                      # inside the worktree, so it goes when the worktree does
bash "$root/.claude/skills/dev-gbv-check/gbv_sweep.sh" "$bin" "$out"   # run_in_background
```

Then arm a Monitor on its events. The file holds nothing but problems plus a final `DONE`, so
every line is worth waking for:

```bash
tail -n +1 -F "$out/events.log" | grep --line-buffered -E "^(FAIL|GBV|CRASH|LOG|DONE)"
```

`timeout_ms` 1800000, and re-arm on expiry: a full sweep takes an hour or more, because a file
whose cases ask for validation spends seconds to minutes per case. `$out/summary.txt` has one line
per file when you want progress.

Run one sweep per `bin/` at a time, and nothing else on the GPU while it runs, not even another
checkout's `just test`. Two processes share `bgpu.log`, and a TDR in either kills both.

**Why one file per process, in series.** A TDR resets the whole adapter, so in a sharded run one
hang kills every shard and the crash logs blame innocent cases. Every process also truncates
`bin/bgpu.log` when it starts, so only a serial run leaves a log you can attribute.

**What is expected, not a bug:**

- `rc=2 No tests ran` means a file whose cases are all Metal-only, for example `QueueFlushIdle_test` and `QueueFlushPool_test`.
- `rc=4`, all skipped: `ShaderCache_test` skips itself because the driver pipeline layer is off under validation.
- `GPU assertion(s) fired` in `DebugAssert_test`: that suite raises them on purpose.
- `[hashedalpha]` failures and `Metal-tuned bound not met on D3D12` warnings are known issues on master. Never fix them.
- Only cases that set `enableGPUValidationLayer = bgl::test::GpuValidationEnabled()` are instrumented when their file runs alone. A fast, clean file may simply not have been validated.

## 5. Triage what the Monitor reports

Work each event while the sweep continues. Edit sources in the worktree, but do not build until
the sweep has finished; a shader fix can be tried at once by copying it into `$bin/shaders/src`
(below).

**`FAIL ... rc=22` + `DXGI_ERROR_DEVICE_HUNG` / `RemoveDevice`: a GPU hang, TDR after 2 s.**
Do not accept "validation is slow" without proof; it was wrong last time. Find the pass:
temporarily make `FrameGraph::Execute` close the list, submit and wait after each pass, and print
the pass name to stderr. Timestamp each line (`| while read l; do echo "$(date +%T.%N) $l"; done`)
and compare it against the removal time in `bgpu.log`. Once the device is removed, fence waits
return at once, so every "ok" printed after that time is a lie. Then bisect the pass's shaders.
Look first for a group barrier after a `return`, then for a loop whose trip count is read from
payload memory (`docs/known_issues.md` has both). Revert the diagnostic before committing.

**Bisect shaders without rebuilding.** Slang compiles from `$bin/shaders/src` at run time, so edit
that copy, back up the original, and rerun one case:
`./bgl_extended_tests.exe "<case name>" --gpu-validation`.
Warnings are errors, and DXC rejects a second `SetMeshOutputCounts` call site, so keep edits
branch-free. The next build overwrites the copy; put the real fix in `libs/`.

**`GBV ... Descriptor type doesn't match shader register type` (UAV in heap, SRV in shader).** A
read-only Slang view (`UploadBuffer`, `EntryBuffer`, `RangeBuffer`, a `StructuredBuffer.Handle`)
has been bound to a `ComputeBuffer`. Bind what the renderer binds there: `UploadBuffer<T>`, or
`CreateStructBuffer` with `isUav = false`.

**`GBV ... heap index out of bounds ... [4294967295]`.** An invalid handle (`0xFFFFFFFF`) reached a
shader. `SetIfValid` checks that the uniform exists, not that the handle is valid. A test harness
that builds its own `DrawData` usually forgot a field `RenderContext` fills, such as
`lighting.env.brdfLut`.

**`GBV ... Uninitialized descriptor ... Heap Index [0]`.** A uniform handle nothing assigned. Slot
0 is the reserved unbound sentinel (`docs/uniforms.md`).

**The messages name no shader.** For a diagnostic, name the PSOs: `m_PipelineState->SetName(...)`
from each shader's `GetDesc().debugName` in `MeshletPipeline_d3d12.cpp` / `ComputePipeline_d3d12.cpp`.
Revert it afterwards.

## 6. Verify, then PR

1. `just build`.
2. Re-run the sweep over only the files that had events: `gbv_sweep.sh "$bin" "$out/rerun" A_test B_test`.
   Every one must be free of `FAIL`, `GBV` and `CRASH`.
3. Run the whole suite the way CI's users do: `just test` (all suites), then
   `just test bgl_extended -- --gpu-validation`. A single TDR anywhere fails every shard, so this run
   is the proof that no hang is left.
4. `just format` on each changed `.cpp`/`.h`/`.slang`, and `just tidy --changed`.
5. Update the docs the fix contradicts, `docs/known_issues.md` above all. Add an entry for a hang or
   validation bug that cost real time to find.
6. Commit on `fix/dev-gbv-<stamp>`, one commit per bug, with the attribution trailer. Push and open
   the PR with `--base dev`, unless step 1 showed the bugs are master's and the user chose master.
   The PR body lists each finding: the symptom, the cause, the fix, and whether master has it too.

Leave the worktree for the user. Remove it with `git worktree remove` only when they say so.
