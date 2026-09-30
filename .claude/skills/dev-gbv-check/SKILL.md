---
name: dev-gbv-check
description: Check origin/dev end to end on Windows/D3D12 - build it in a gitignored worktree, run every suite, run bgl_tests under GPU-based validation one file at a time with a Monitor on errors only, fix what it finds, and open a bugfix PR against dev. Use when asked to check, verify or GBV-test the dev branch.
---

# Check origin/dev under GPU-based validation

The goal is a PR against `dev` that fixes every bug the build, the suites and a `--gpu-validation`
run of `bgl_tests` turn up. Work in a worktree so the user's checkout is never touched.

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

### 2b. A release build for the validation sweep

GPU-based validation patches every shader, and debug shaders carry debug info and the `dbg_raise`
bodies, so there is more to patch. Measured under `--gpu-validation`: `Capture_test` 44 s debug
against 7 s release, `Resize_test` 60 s against 8 s. No preset builds tests in release, so write a
user preset into the worktree. `CMakeUserPresets.json` is gitignored, so nothing reaches the tree:

```bash
cat > "$wt/CMakeUserPresets.json" <<'JSON'
{
  "version": 3,
  "configurePresets": [
    {
      "name": "gbv-release",
      "binaryDir": "${sourceDir}/build/gbv-release",
      "inherits": [ "windows-ninja-msvc-dx12-release" ],
      "cacheVariables": { "BUILD_TESTS": "ON" }
    }
  ],
  "buildPresets": [
    { "name": "gbv-release", "configurePreset": "gbv-release", "configuration": "Release" }
  ]
}
JSON
(cd "$wt" && just build bgl_tests --preset gbv-release)   # run_in_background
```

A release build defines no `BERNINI_GPU_DEBUG`, so the GPU `dbg_assert`s are gone, and any test file
wrapped in `#if defined(BERNINI_GPU_DEBUG)` has no cases in it. Sweep those files from the debug
build as well (step 4); list them with:

```bash
grep -ln "^#if defined(BERNINI_GPU_DEBUG)" "$wt"/libs/bgl/tests/src/*_test.cpp | xargs -n1 basename | sed 's/\.cpp$//'
```

## 3. The other suites, plainly

```bash
just test --no-build assetlib bgl_tests bgpu core crowdlib editor_plugin editor_tests gamelib scripts
```

Only `bgl_tests` takes `--gpu-validation`. Check `just test --list` for new suites first
and add any the list above misses.

## 4. The GPU-validation sweep, with a Monitor on errors

```bash
bin="$wt/build/gbv-release/bin"     # the release build from step 2b
dbg=$(dirname "$(cd "$wt" && just exes --target bgl_tests | tail -1)")
out="$wt/.gbv"                      # inside the worktree, so it goes when the worktree does
bash "$root/.claude/skills/dev-gbv-check/gbv_sweep.sh" "$bin" "$out"   # run_in_background
# then, once it is DONE, the debug-only files from step 2b against the debug build:
bash "$root/.claude/skills/dev-gbv-check/gbv_sweep.sh" "$dbg" "$out/debug" CullInstances_test DebugAssert_test ...
```

Then arm a Monitor on its events. The file holds nothing but problems plus a final `DONE`, so
every line is worth waking for:

```bash
tail -n +1 -F "$out/events.log" | grep --line-buffered -E "^(FAIL|GBV|CRASH|LOG|DONE)"
```

`timeout_ms` 1800000, and re-arm on expiry. `$out/summary.txt` has one line per file when you want
progress. The first draw through each pipeline pays the debug layer's patching, once per process,
because renderers on one GPU context share their PSOs. After that a validated frame costs about 2x an
unvalidated one, 3.5x with TAA.

**A clean sweep is only worth something if validation was reporting.** If a run is suspiciously fast
or quiet, plant a canary and check it fires. Bind a writable buffer where a shader reads a read-only
one, then run that one file: in `CullInstances_test`, set `cull["gUniforms"]["cullView"]` to
`visibility.GetBufferHandle()`. Expect a `Descriptor type doesn't match` message in `bin/bgpu.log`,
then revert the file. **Never turn off the debug layer's synchronized command-queue validation**
(`SetEnableSynchronizedCommandQueueValidation(FALSE)`) to speed a run up: frames then cost what
unvalidated ones do because GPU-based validation stops reporting, and the canary goes silent.

**A file that is slow under validation is re-rendering one pass many times.** Validation needs one
case per distinct pass, configuration or resource lifetime, not every point of a measurement
sweep. Call `bgl::test::SkipUnderGpuValidation()` first thing in each redundant case, as
`TaaResolve_test` and `HashedAlpha_test` do. Keep every case that creates or destroys resources
mid-run: those catch teardown-order bugs. Show the user the keep and skip list before editing.

Run one sweep per `bin/` at a time, and nothing else on the GPU while it runs, not even another
checkout's `just test`. Two processes share `bgpu.log`, and a TDR in either kills both.

**Why one file per process, in series.** A TDR resets the whole adapter, so in a sharded run one
hang kills every shard and the crash logs blame innocent cases. Every process also truncates
`bin/bgpu.log` when it starts, so only a serial run leaves a log you can attribute.

**What is expected, not a bug:**

- `rc=2 No tests ran` means a file whose cases are all Metal-only, for example `QueueFlushIdle_test` and `QueueFlushPool_test`.
- `rc=4`, all skipped: `ShaderCache_test` skips itself because the driver pipeline layer is off under validation.
- `GPU assertion(s) fired` in `DebugAssert_test`: that suite raises them on purpose.
- `RTV pool exhausted` and `Bloom chain ... could not be allocated` in `Bloom_test` and `QueueSync_test`, and `Draw bucket ceiling (3) reached` in `DrawBucketTable_test`: cases that exhaust a pool or a ceiling on purpose. They are `[error]` log lines, not validation messages.
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
`./bgl_tests.exe "<case name>" --gpu-validation`.
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

1. `just build`, and `just build bgl_tests --preset gbv-release`.
2. Re-run the sweep over only the files that had events: `gbv_sweep.sh "$bin" "$out/rerun" A_test B_test`.
   Every one must be free of `FAIL`, `GBV` and `CRASH`.
3. Run the whole suite the way CI's users do: `just test` (all suites, debug), then the validated
   suite sharded from the release build:
   `(cd "$bin" && ./bgl_tests.exe --gpu-validation --shard-count 4 --shard-index N)`
   for each N, or `just test bgl_tests -- --gpu-validation` against the debug build. A single
   TDR anywhere fails every shard, so this run is the proof that no hang is left. Sharded output is
   held until each shard exits; judge progress by fresh files in `bin/assets/golden/` and by CPU,
   not by the console.
4. `just format` on each changed `.cpp`/`.h`/`.slang`, and `just tidy --changed`.
5. Update the docs the fix contradicts, `docs/known_issues.md` above all. Add an entry for a hang or
   validation bug that cost real time to find.
6. Commit on `fix/dev-gbv-<stamp>`, one commit per bug, with the attribution trailer. Push and open
   the PR with `--base dev`, unless step 1 showed the bugs are master's and the user chose master.
   The PR body lists each finding: the symptom, the cause, the fix, and whether master has it too.

Leave the worktree for the user. Remove it with `git worktree remove` only when they say so.
