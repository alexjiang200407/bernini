# A looping clip plays one frame too long

`frameCount` means two different things in this tree, and playback implements the wrong one for every
clip the importer produces. Both animated tiers are affected, because both share
[`clip_playback.slang`](libs/bgl/shaders/src/clip_playback.slang).

## The two conventions

**What the importer writes.** `frameCountFor` ([gltf_skin.cpp](libs/assetlib/src/gltf_skin.cpp)) is
`round(duration * sampleRate) + 1` — "both ends included" — so a 1-second clip at 30 fps is **31**
frames, `f0 .. f30`, with `f30` at `t = 1.0`. And `clip.loop` is set *only* when `posesMatch(firstPose,
lastPose)`: a clip is a loop precisely because its last frame duplicates its first. So for a looping
clip the duplicate is **counted inside `frameCount`**, and one cycle is `frameCount - 1` frame
intervals.

The VAT bake carries this through unchanged (`baked.frameCount = clip.frameCount`) and pads *another*
row after it, so a real `.bvat` has `frameCount + 1` rows where row `frameCount - 1` is the authored
duplicate and row `frameCount` is the pad.

**What the shader assumes.** `ClipFrames` wraps with `fmod(frames, frameCount)`, and
`ClipFrameIndices` wraps the upper index `frameCount -> 0`. That is the convention of `frameCount`
*distinct* frames with an implicit wrap — no duplicate counted.

## What it costs

With `frameCount = 31`, playback runs over 31 intervals instead of 30:

| `frames` | `f0` | `f1` | poses blended |
|---|---|---|---|
| `[29, 30)` | 29 | 30 | `S29 -> S30`, and `S30 == S0` — the correct seam |
| `[30, 31)` | 30 | 0 (wrapped) | `S30 -> S0`, **both the same pose** |

So every loop holds still for one frame interval (33 ms at 30 fps) and the clip runs ~3.2 % slow. On a
walk cycle it reads as a hitch once per stride.

## Why no test catches it

`bgl`'s fixtures synthesize clips by hand and use the *shader's* convention, not the importer's —
`VatPlayback_test`'s `c_LoopClip` is `frameCount = 2` with two **distinct** frames, so wrapping `1 -> 0`
is right for it. The skinned suites only exercise `loop == 0`. Nothing in the tree feeds an
importer-produced looping clip to a playback assertion, which is the gap that hid this.

## The fix, and why it is not a one-liner

Pick the importer's convention — it is the one all real content has — and then make everything agree:

1. `ClipFrames` wraps over `frameCount - 1`. Safe: `clip.loop` is only ever set when `frameCount > 1`.
2. `ClipFrameIndices`' `f1 == frameCount -> 0` wrap becomes unreachable and should go; the upper index
   tops out at `frameCount - 1`, which holds the authored duplicate of frame 0 and is the right texel
   to read. The VAT pad row stays unread, so [vat.md](docs/vat.md)'s rule still holds.
3. **Every hand-authored fixture has to be re-cut**, because they encode the other convention: a
   2-frame loop becomes a 3-frame one whose last frame repeats its first. `VatPlayback_test`'s
   seam and `RenderJob::time` sections change frames under the fix and would otherwise fail — those
   failures are the fixture being wrong, not the fix.
4. Re-shoot the VAT golden images that pin a frame at a time.

## Trigger

Any clip authored to loop that a person actually watches — the first walk cycle in a level, or the
Animation panel left playing. It is invisible on a two-frame test quad and obvious on a 30-frame
stride.
