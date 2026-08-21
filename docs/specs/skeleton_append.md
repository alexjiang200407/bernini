# Appending a bone strands every clip — and it should not

**Status: deferred, deliberately.** Nothing in this document is built. It records a problem that is
currently cheap to ignore, the point at which it stops being cheap, and the design settled on so
that whoever picks it up does not re-derive it. Written 2026-08-21. Delete this file when the
remap lands.

---

## The problem

A clip set and a skinned mesh both address their skeleton by **bare bone index**, and a wrong
index is undetectable from the pose it produces. The guard is `skeletonSignature`
([libs/assetlib/src/skeleton.cpp](../../libs/assetlib/src/skeleton.cpp), `:18-27`): a hash over
every bone's **name and parent, in order**. A `.banim` (and a `.bvat`) records the signature of
the rig it was cooked against, and a clip set whose signature differs is refused at load —
`AssetManager` throws *"was cooked against a different version of '…'"*
([libs/gamelib/src/AssetManager.cpp](../../libs/gamelib/src/AssetManager.cpp), `~:498`). A
skinned `.bmesh` records **only the rig's path**, no signature
([BMesh.h](../../libs/assetlib_structs/include/assetlib_structs/BMesh.h), `:33`) — the mesh↔rig
door has no guard at all, so the same rig edit that refuses the clips loudly lets the mesh's
`joints0` skin against the wrong bones silently.

The guard that exists is correct, but it is **stricter than the change it guards against** —
while the missing one is looser. An artist's added bone lands wherever the depth-first walk
places it ([gltf_skin.cpp](../../libs/assetlib/src/gltf_skin.cpp), `:49-63`), so in bone-index
space it is an *insert*: indices after it shift, the signature changes, and every clip set cooked
against the rig refuses to pair. Yet no *data* was invalidated — every old bone still exists,
findable **by name** — and the only recovery offered is re-exporting every clip source against
the new rig.

Appends are not the rare skeleton edit; they are the routine one. Over a character's production
life artists add:

- **attachment sockets** — weapon grips, prop and mount points, banners and shields: the battle
  game's bread and butter, added whenever gameplay asks;
- **deformation helpers** — twist/roll and corrective bones, added when animation review shows a
  shoulder collapsing, which is only visible once real clips exist;
- **facial and dynamics bones** — added when a unit graduates to camera-facing, or when capes and
  tails arrive.

Renames, deletions and reorders — the edits that genuinely invalidate cooked data — are rare, and
artists know they are breaking something when they make them. The industry-standard engines are
tolerant of exactly this split: Unreal binds animation to bones **by name**, so a clip authored
before a socket existed still plays after it is added, and a bone the clip does not animate takes
the reference pose. Here, the same append is a full re-export event for every clip source of that
rig.

The signature's own doc comment ([libs/assetlib/include/assetlib/skeleton.h](../../libs/assetlib/include/assetlib/skeleton.h),
`:10-19`) already draws one such line deliberately — the bind pose is *excluded* so that
re-authoring a rest pose does not re-cook every clip. This spec extends the same reasoning one
step — an added bone should not re-cook them either — and closes the unguarded mesh side while
it is at it.

## When it stops being cheap

Today every rig in the project comes from purchased packs, whose skeletons nobody edits — the
problem is real but dormant. It wakes when either:

| Trigger | Why it changes the calculus |
|---|---|
| **Original characters enter production** | Twist bones, correctives and facial joints arrive mid-production by nature; each append re-exports every clip of that rig. |
| **Units need attachment sockets** | The battle game's units hold weapons; the first "give the soldier a grip bone" request turns one append into a re-export of that rig's whole clip library. |

## The design settled on: remap by name at load

Keep the signature as the fast path, and add a **name-keyed index remap** where it mismatches:

- Signatures equal → today's zero-cost path, untouched.
- Signatures differ → build an old→new index map **by bone name**. That requires the one format
  change this design costs: today neither container stores the old rig's bone names —
  `AnimationSet` persists only `{signature, boneCount}` about its rig
  ([banim_io.cpp](../../libs/assetlib/src/banim_io.cpp), `:41-44`; bone names live in the
  *skeleton's* string pool, which the append already rewrote) and `BMesh` stores a bare path. So
  the `.banim` gains its cooked rig's **bone-name list**, and the skinned `.bmesh` gains the
  signature + name list it never had (which is also what closes its unguarded door). Only
  containers cooked after that lands are remappable; the existing ones re-cook once, at the
  format change, and never again for an append.
- With the map built: if **every stored bone resolves** in the current skeleton — the old set is
  a subset, i.e. the change was additive — remap indices at load: a clip's tracks target their
  bones' new indices, a mesh's `joints0` likewise, and bones the clip does not animate take the
  bind pose, exactly as a name-binding engine behaves.
- Any stored bone that does **not** resolve → today's hard refusal, unchanged. A rename or
  deletion means the data genuinely no longer has a target, and guessing is the failure the
  signature exists to prevent.

Properties worth keeping when building it:

- After the one-time format change, the remap is a *load-path* affordance — an append rewrites no
  cooked file, and a project that never appends pays only the name list's bytes.
- It composes with the (planned) source+cache asset model, and is **not** made redundant by it:
  regeneration fixes a rig's own source group, but a *clips-only* group from another `.glb` —
  attached by signature — is current by its own key when the shared rig grows, and the remap is
  exactly what keeps it playing. That residual case is the strongest reason to build this even
  after that model lands. An explicit rewrite (`migrate`) may bake the remapped indices down, at
  which point the stored signature updates and the fast path returns.
- Consumers that bake indices downstream must be audited when this is built: the VAT bake and the
  posed-bounds measurement both walk bones by index, and a remap must land *before* they read.

## Non-goals

- **Retargeting** — playing a clip on a *different* skeleton (other proportions, other bone set).
  This spec is only about the same rig growing.
- **Tolerating renames, deletions or reorders.** Those stay hard refusals; the data for them does
  not exist, and pretending otherwise is the silent mis-pose the signature was built to catch.
