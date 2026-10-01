---
name: dev-gbv-check
description: Check origin/dev end to end on Windows/D3D12 in the main checkout - build it, run every suite once, run bgl_tests sharded under GPU-based validation with a log per shard and a Monitor on errors only, fix what it finds, and open a bugfix PR against dev. Use when asked to check, verify or GBV-test the dev branch.
---

# Check origin/dev under GPU-based validation

The goal is a PR against `dev` that fixes every bug the build, the suites and a `--gpu-validation`
run of `bgl_tests` turn up. Three rules make it fast:

- **The main checkout, not a worktree.** Its build dirs are warm, so a build is incremental. A fresh
  worktree configures from nothing and rebuilds everything, twice (debug and release).
- **Always sharded.** Every validated process runs at once, each with its own log (`gbv_shards.sh`).
- **Every test runs once.** `bgl_tests` runs only under validation, never also plainly. After a fix,
  rerun the cases that failed, by name, never their file or their suite.

## 1. Branch in place

The checkout must have no tracked changes, because switching branches would carry them along.
If `git status --porcelain --untracked-files=no` prints anything, stop and ask the user.

```bash
root=$(git rev-parse --show-toplevel)
git -C "$root" fetch origin
start_branch=$(git -C "$root" branch --show-current)    # tell the user, to switch back to at the end
stamp=$(date +%Y%m%d-%H%M)
git -C "$root" switch -c "fix/dev-gbv-$stamp" origin/dev
git -C "$root" lfs pull
out="$root/build/gbv/$stamp"                              # build/ is gitignored
```

See whether a bug is `dev`'s at all: `git rev-list --count origin/dev..origin/master` and
`origin/master..origin/dev`. When dev is master plus commits, check every finding against master
(`git log -S"<code>" origin/master -- <file>`), and tell the user which branch has it. They choose
whether it goes to master or is folded into the dev PR.

## 2. Build both, at once

GPU-based validation patches every shader, and debug shaders have more to patch. Measured under
`--gpu-validation`: `Capture_test` 44 s debug against 7 s release, `Resize_test` 60 s against 8 s.
So validation runs from a release build of `bgl_tests`, and the other suites from the debug build.

No preset builds tests in release. If `CMakeUserPresets.json` has no `gbv-release` preset, add one;
if the file exists, merge it in rather than overwriting it. The file is gitignored.

```json
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
```

Run both with `run_in_background`, at the same time. They share the job budget.

```bash
just build
just build bgl_tests --preset gbv-release
```

Warnings are errors under the strict flags. Fix what fails, then continue. Never build while
anything runs from a `bin/` directory: the exe is replaced under it.

A release build defines no `BERNINI_GPU_DEBUG`, so any test file wrapped in
`#if defined(BERNINI_GPU_DEBUG)` has no cases there. Those run from the debug build (step 3):

```bash
debug_only=$(grep -ln "^#if defined(BERNINI_GPU_DEBUG)" "$root"/libs/bgl/tests/src/*_test.cpp \
	| xargs -n1 basename | sed 's/\.cpp$//; s/.*/[#&]/' | paste -sd,)
```

## 3. Run everything once

GPU suites and validation must not overlap: a TDR in one process resets the adapter under all of
them. CPU-only suites overlap freely.

```bash
# a. The suites that use the GPU, plainly. Every one but bgl_tests, which runs only in (b).
just test --no-build bgpu crowdlib gamelib editor_tests

# b. Then, together, all in the background:
dbg=$(dirname "$(just exes --target bgl_tests | tail -1)")
bash "$root/.claude/skills/dev-gbv-check/gbv_shards.sh" "$root/build/gbv-release/bin" "$out/release"
bash "$root/.claude/skills/dev-gbv-check/gbv_shards.sh" "$dbg" "$out/debug" "$debug_only"
just test --no-build assetlib core editor_plugin scripts      # CPU only: they link nothing that uses a device
```

Check `just test --list` first. A new suite goes in (a) if it links `bgpu`, `bgl`, `gamelib`,
`crowdlib` or the editor, and in the CPU list otherwise.

Arm one Monitor on both event files. They hold nothing but problems plus a final `DONE` each, so
every line is worth waking for:

```bash
tail -n +1 -F "$out/release/events.log" "$out/debug/events.log" \
	| grep --line-buffered -E "^(FAIL|GBV|CRASH|DONE)"
```

`timeout_ms` 1800000; re-arm on expiry from the line it reached. Each shard's output is held until
it exits, so judge progress by fresh files in `bin/assets/golden/` and by CPU, not by the console.

Measured on 06e99bd9 with 4 shards: the release run took 18 min, and its shards finished at 12, 13,
15 and 18 min. `summary.txt` gives each shard's time, and `slowest.txt` the 25 slowest cases. For more
shards, set `GBV_SHARDS`; `GBV_SEED` reproduces a run's partition.

**A clean run is only worth something if validation was reporting.** If a run is suspiciously fast
or quiet, plant a canary. In `CullInstances_test`, set `cull["gUniforms"]["cullView"]` to
`visibility.GetBufferHandle()`, which binds a writable buffer where the shader reads a read-only one.
Build the debug `bgl_tests`, then run `gbv_shards.sh "$dbg" "$out/canary" "[#CullInstances_test]"`.
Expect `Descriptor type doesn't match` in exactly one shard's events, then revert the file.
**Never turn off the debug layer's synchronized command-queue validation**
(`SetEnableSynchronizedCommandQueueValidation(FALSE)`) to speed a run up. Frames then cost what
unvalidated ones do because GPU-based validation stops reporting, and the canary goes silent.

**A case that is slow under validation is usually re-rendering one pass many times.** Validation needs
one case per distinct pass, configuration or resource lifetime, not every point of a measurement
sweep. Read `slowest.txt`, and call `bgl::test::SkipUnderGpuValidation()` first thing in each redundant
case, as `TaaResolve_test` and `HashedAlpha_test` do. Keep every case that creates or destroys
resources mid-run: those catch teardown-order bugs. Show the user the keep and skip list before editing.

Nothing else may use the GPU while the validated shards run, not even another checkout's `just test`.

**What is expected, not a bug:**

- Shard `rc=4`, or a case that reports skipped: `ShaderCache_test` skips itself, because the driver pipeline layer is off under validation.
- `GPU assertion(s) fired` from `DebugAssert_test`: that suite raises them on purpose.
- `RTV pool exhausted` and `Bloom chain ... could not be allocated` from `Bloom_test` and `QueueSync_test`, and `Draw bucket ceiling (3) reached` from `DrawBucketTable_test`. These cases exhaust a pool or a ceiling on purpose. They are `[error]` log lines, not validation messages.
- `[hashedalpha]` failures and `Metal-tuned bound not met on D3D12` warnings are known issues on master. Never fix them.

## 4. Triage what the Monitor reports

Work each event as it arrives, but do not build until every run from that `bin/` has finished. A
shader fix can be tried at once by copying it into `bin/shaders/src` (below).

**Which case.** A `GBV` or `CRASH` line names its shard. List the shard's cases in the order they ran
with the shard's own arguments: the spec, `-# --order rand --rng-seed <seed from summary.txt>
--shard-count N --shard-index i`, then `--list-tests`. A `FAIL` line names the shard's last
completed case, and the case that was running is the next one in that list. Then run only the
suspect case: `./bgl_tests.exe "<case name>" --gpu-validation` from `bin/`. Catch2 splits a spec at
commas, so for a name that has one, put `*` in the comma's place.

**A TDR (`DXGI_ERROR_DEVICE_HUNG` / `RemoveDevice`): every shard dies with it.** The culprit is the
shard whose `shard<N>.bgpu.log` has the earliest removal time, and the case it was running when that
happened. The other shards' failures are collateral; do not chase them. Do not accept "validation
is slow" without proof; it was wrong last time. Find the pass: temporarily make
`FrameGraph::Execute` close the list, submit and wait after each pass, and print the pass name to
stderr. Timestamp each line (`| while read l; do echo "$(date +%T.%N) $l"; done`) and compare it
against the removal time in `bgpu.log`. Once the device is removed, fence waits return at once, so
every "ok" printed after that time is a lie. Then bisect the pass's shaders. Look first for a group
barrier after a `return`, then for a loop whose trip count is read from payload memory
(`docs/known_issues.md` has both). Revert the diagnostic before committing.

**Bisect shaders without rebuilding.** Slang compiles from `bin/shaders/src` at run time, so back up
that copy, edit it, and rerun the one case. Warnings are errors, and DXC rejects a second
`SetMeshOutputCounts` call site, so keep edits branch-free. The next build overwrites the copy; put
the real fix in `libs/`.

**`Descriptor type doesn't match shader register type` (UAV in heap, SRV in shader).** A read-only
Slang view (`UploadBuffer`, `EntryBuffer`, `RangeBuffer`, a `StructuredBuffer.Handle`) has been bound
to a `ComputeBuffer`. Bind what the renderer binds there: `UploadBuffer<T>`, or `CreateStructBuffer`
with `isUav = false`.

**`heap index out of bounds ... [4294967295]`.** An invalid handle (`0xFFFFFFFF`) reached a shader.
`SetIfValid` checks that the uniform exists, not that the handle is valid. A test harness that builds
its own `DrawData` usually forgot a field `RenderContext` fills, such as `lighting.env.brdfLut`.

**`Uninitialized descriptor ... Heap Index [0]`.** A uniform handle nothing assigned. Slot 0 is the
reserved unbound sentinel (`docs/uniforms.md`).

**The messages name no shader.** For a diagnostic, name the PSOs: `m_PipelineState->SetName(...)`
from each shader's `GetDesc().debugName` in `MeshletPipeline_d3d12.cpp` / `ComputePipeline_d3d12.cpp`.
Revert it afterwards.

**A suite fails outside validation.** Check whether it is time- or order-dependent before calling it
flaky. Last time, one case failed only when the build was more than an hour old, and another only
when the GPU kept up with the CPU.

## 5. Verify the fixes, then PR

1. Build what the fixes touched.
2. Rerun **only the cases that failed**, by name. Run a validation finding under `--gpu-validation`
   from the build it came from. A case that failed intermittently gets several runs. Nothing else
   reruns: everything else already passed once against code the fixes do not reach. If a fix does
   reach shared code, a renderer pass or a library seam, say so and ask before running more.
3. `just format` on each changed `.cpp`/`.h`/`.slang`, and `just tidy --changed`.
4. Update the docs the fix contradicts, `docs/known_issues.md` above all. Add an entry for a hang or
   validation bug that cost real time to find.
5. Commit on `fix/dev-gbv-<stamp>`, one commit per bug, with the attribution trailer. Push and open the
   PR with `--base dev`. The body lists each finding (symptom, cause, fix, and whether master has
   it too) and what ran.

Then tell the user the branch is left checked out, and the branch it started from (`$start_branch`).
Delete `$out` and the `bin/.gbv-shard*` link directories when they say so.
