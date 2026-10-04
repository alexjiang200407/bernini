# Toon Shading Rig — the runtime

A toon shading rig (`ToonShadingRigDesc`, the Shading Rig of Petikam, Anjyo & Rhee 2021) is a face's
art-directed shadow and light edits, each keyed on the sun's direction in head space. This page is
what happens to one each frame: which placements are evaluated, where a placement's head is, how the
keys become the block a face's pixels read, and how those pixels find it. The API is in
[bgl API](bgl_api.md); the material half the toon character model shades with is in
[Game-defined surfaces](game_defined_surfaces.md) § Toon surfaces.

## Who holds one, and who is evaluated

A rig is a scene object; a placement takes one with `ISceneView::SetToonShadingRig`, and every slot of
an instance block takes the block's (`MeshInstanceBlockDesc::toonShadingRig`). A view keeps no word per
placement for it. It keeps a dense list of **ranges**, `idl::ToonShadingRigRange` -- a hero is a range
of one, a block a range of its capacity -- rebuilt on the CPU when a rig is set or cleared or a holder
deleted, each carrying the running count of placements before it
([scene/ToonShadingRigState.h](../libs/bgl/src/scene/ToonShadingRigState.h)).

Each draw, **Toon Shading Rigs** ([passes.md](passes.md)) runs one thread per placement of every
range, finding its range by a binary search over those counts. A placement is selected when it is not
hidden, its head sphere -- `headRadius` times the placement's uniform scale, about the head's origin
-- lies in the draw's frustum, and the head's projected diameter,
`2 * radius * pixelsPerUnit / distance`, exceeds the rig's `fadeEndPixels`. A selected placement takes
the next block of the view's pool with an atomic; the pool holds `cToonShadingRigPoolCapacity`, and
one past it is not evaluated. The selection is per draw: a frame's second draw of a view selects again
for its own camera.

## How the draws find the block

`MeshInstance::flags` carries the block's index plus one from `idl::cToonShadingRigSlotShift`, under
`cToonShadingRigSlotMask`; the bits below are `MeshInstanceFlag`'s. The pass writes them on every
rigged placement every draw -- the block it took, or zero -- and nothing else writes them: the CPU
writes only `MeshInstanceFlag` bits, and an instance block's writer only `kHidden`. So a placement
with no rig always reads zero, and costs nothing: no buffer, no word, no thread. A placement whose rig
is cleared leaves the pass's reach, so its record is uploaded again from the CPU, which holds no slot
bits.

## The head, and the light it sees

The head's frame is the placement's transform, then the head bone's model transform if the rig names
one, then the rig's `headToBone`. No buffer holds a bone's model transform between passes; a palette
slot, a pose-pool slice and a bone anim table frame all store `model * inverseBind`
([skinning.md](skinning.md)), so the pass multiplies the inverse bind back out of whichever the
placement drew this frame: its palette (the per-instance source), its pose-pool slice or its rig's
table frames blended by the dominant weight (the automatic source), or the table frames its clip
resolves to (the table source).

The sun goes into head space through the head's rotation, its scale divided out of each axis, and is
remapped (`FaceLightDesc`): its azimuth about +Y from +Z and its elevation above the XZ plane, the
azimuth swung toward the front by `azimuthFadeAmount` as the elevation rises from `azimuthFadeStart`
to `azimuthFadeEnd` (a smoothstep), then both clamped. The block keeps that light in world space, and
in its `w` the fade, `saturate((pixels - fadeEndPixels) / (fadeStartPixels - fadeEndPixels))`.

## The edits

Each edit's keys are blended for the face light by normalized spherical-Gaussian weights,
`exp(keySharpness * (dot(key.light, light) - 1))`, every field weighed alike, so the blend stays in the
hull of the keys. The paper's exact radial-basis interpolation is not carried: between two close keys
it overshoots both. An edit whose keys' gains are all of one sign is held to it. A mirrored edit is
evaluated twice, the second time with the light's X flipped, into the next slot with its mirror flag
set. Each slot stores the edit's position, its gain, its frame -- Z toward the position from the head's
origin, X across it from head-space up -- and its shape, clamped to what the pixel stage can draw
([idl/ToonShadingRigBlock.slang](../libs/bgl/shaders/src/idl/ToonShadingRigBlock.slang)).

## The pixels

A toon character's draw bucket at rest draws through `MSToon` -- in `programs.forward.StaticMesh`
and `SkinnedMesh`, a copy of `MSMain` -- whose `ToonVSOut` is `ForwardVSOut` plus one
`nointerpolation` word: the placement's block index plus one, read off its flags word once per
mesh group. No other bucket draws through it, so no PBR or lit draw carries the word, and a
character's dissolve lane and the shared blend program, which every surface shares, carry none
either: a dissolving or blended character shades without its rig. The character's generated
programs then shade through `GameToon*Program`, which loads the block from the view's pool
(`MaterialData::toonShadingRigBlocks`) when the word names one.

`ShadeToonCharacterFace` (`lib.math.ToonShading`) is the cel model of
[Game-defined surfaces](game_defined_surfaces.md) § Toon surfaces with two changes on a pixel that
is `face`, each by as much as it is:

- **The light** is the block's face light rather than the sun, blended toward it by `face`.
- **The terminator** moves by the sum of every slot's push, times `face` and the block's fade. The
  pixel goes into head space through `headFromWorld`, its normal through the same rotation, both
  with X flipped on a mirrored slot. A slot's push (`ToonShadingRigSlotOffset`) is the Shading Rig's
  shape: the direction from the pixel to the edit, projected azimuthally about the edit's axis and
  scaled by its size, turned by its rotation, twisted about `(bulge, bend)` by
  `10 * (bulge * x + bend * y)` radians, and fallen off as `exp(-(e x² + |y|^(2 - sharpness) / e))`
  with `e = 1 - anisotropy`; then faded out over the last quarter of its radius, cut where the twist
  passes a quarter turn, and scaled by how far the pixel's normal -- pulled toward the head's
  sphere by its normal smoothing -- faces the edit.

## Cost

Per pixel of a face, every slot of its block; nothing on a pixel that is no face, and nothing on any
other surface. Per rigged placement per draw, one thread: a binary search over the ranges, the head's frame, the
sun's remap, and for a selected one every key of every edit -- at most `cMaxToonShadingRigSlots`
slots of `cMaxToonShadingRigKeysPerEdit` keys. A view's pool is `cToonShadingRigPoolCapacity` blocks of
848 bytes, allocated with its first range.
