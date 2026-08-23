# Animation compression

`.banim` stores a resampled pose per bone per frame, uncompressed: `boneCount * frameCount` 40-byte
`Transform`s. A 663-bone character with 2,254 frames of clip is **59.7 MB**, which is the file
exactly, and roughly 780 ms to deserialize in a debug build.

Nothing compresses it today. This records what was measured when something nearly did, so the next
attempt starts past it rather than at it.

## The trigger

A project whose clip sets dominate its load time or its GPU residency. One AAA character is 60 MB;
a cast of them is the frame's largest allocation before anything is drawn. Until then the cost is
paid once per rig at load and it is tolerable.

## What was tried, and why it was dropped

Collapsing **constant tracks** — a bone that never moves during a clip stored once rather than once
per frame. Written, tested and then dropped on its own measurements (`feat/calc-pose-bounds-too-slow`,
2026-08-23), because they did not support it:

| | Constant tracks | Pool saved |
|---|---|---|
| `cha800_00`, whole `Transform` | 928 / 3315 (28%) | **7.8%** |
| `cha800_00`, per T/R/S component | translation 929, rotation 1270, scale 1510 | **12.9%** |
| The test project's Coyote | 0 / 870 exact, 3% within 1e-5 | **0%** |

The distribution is what kills it. On that character, `blink_loop` is 93% constant over 181 frames —
only the eyelids move — but the three body clips are 2,069 of the 2,254 frames and carry **two**
constant tracks out of 663 each. A body clip moves everything a little.

Two traps that measurement exposed, both worth avoiding again:

- **Counting glTF source channels is not counting tracks.** A joint has three channels; a bone is
  constant only when translation, rotation *and* scale are. Counting channels with a tolerance said
  75% where counting tracks exactly said 28%.
- **Measure the resampled pool, not the source.** The importer resamples to a fixed rate, and that is
  what the file stores. On the Coyote, exact equality found nothing a 1e-5 tolerance would have
  found either — 3% — so resampling jitter was not hiding a win.

## What to build instead

Quantization, which is where the order of magnitude is: rotations as three 16-bit components with `w`
reconstructed from the other three, translations and scales 16-bit normalized to a per-clip range.
That is roughly 10x on its own, against constant-track collapse's 1.1x, and it is what
[ACL](https://github.com/nfrechette/acl) — Unreal's default codec since 4.25 — does before anything
else. Error is then the whole design problem: a bound per bone, checked against the un-quantized pose,
and a test that pins it.

Two things the dropped attempt got right and a real one should keep:

- **Collapse on disk, expand at load.** The sample pool is frame-major and the GPU indexes it
  directly (`PoseSkinned.slang` reads `firstFrame * boneCount + bone`, and `Scene.cpp` uploads the
  pool wholesale), so a stored form that differs from the in-memory one costs nothing downstream.
  Changing what the *GPU* indexes is a second, larger change — IDL, upload and shader — and it should
  be justified by its own measurement of GPU residency, not folded in.
- **A layout change is one edit.** Bump `c_BAnimBakeToken` in `src/bake_tokens.h`; every older file
  becomes a cache miss and regenerates from its source. `TokenCanary_test` fails a layout change made
  without the bump.
