# uniforms-set-if-valid — implementation plan

## Context

Binding a reflected constant buffer is three lines per member. A variant may not declare a field, so
every optional write is guarded:

```cpp
if (auto u = tonemap["sceneColor"]; u.IsValid())
{
    u = args.source;
}
```

There are roughly fifty of these across `ForwardPass`, `TaaResolvePass`, `PostProcessPass`,
`SkyboxPass` and `CompactInstancesPass`. The guard is the whole point of a reflected mirror — it is
what lets one binder serve a family of PSO variants — but at three lines a member it buries the
binding in scaffolding, and the named `u` exists only to be tested and assigned once.

The guard is also silent by design, and `docs/uniforms.md` is explicit that `IsValid() == false`
cannot distinguish "this variant does not declare the field" from "the name is wrong". The one
defence is `FindUnknownMembers`, resolved once when the PSO family is built. **Only `ForwardPass`
calls it.** `TaaResolvePass`, `PostProcessPass` and `SkyboxPass` bind 31 names between them with no
protection against a shader rename at all, and the helper that would give it to them
(`ValidateBinderNames`) is stuck in `ForwardPass.cpp`'s anonymous namespace.

## Decisions

- **ADR-1 — `Uniforms::AccessorBase::SetIfValid(value)` becomes the spelling of the optional write.**
  A guarded assignment is one expression naming the member once, and the whole existing assignment
  overload set is reached through it unchanged, so a resource handle, a scalar and a matrix all keep
  their current rules. *Rejected: making a bare `operator=` silently no-op on an absent member, which
  removes the scaffolding too — but it takes the loud case with it, and a required member's typo
  would then write nothing and report nothing.*

- **ADR-2 — every guarded site in `bgl` converts in this PR.** *Rejected: adding the method and
  converting only the pass that prompted it, because a second spelling that lives beside the first is
  how the two start disagreeing; a library's members are not the place for it.*

- **ADR-3 — `operator=` stays the required form; no loud counterpart is added.** A member that must
  exist is written plainly and throws when it does not, which is what `SkyboxPass`'s `clipToWorld`
  and `CompactInstancesPass`'s buffer binds already do. *Rejected: a symmetric `Set()` that throws
  naming the member, because two new methods make the reader choose between three spellings where the
  distinction is already carried by whether a guard is written at all.*

- **ADR-4 — `ValidateBinderNames` moves out of `ForwardPass.cpp` into
  `libs/bgl/src/passes/binder_names.h`, and `TaaResolvePass`, `PostProcessPass` and `SkyboxPass` call
  it from `Init`.** `SetIfValid` makes a silent guard cheaper to write, so the check that tells a
  variant's absent field from a typo has to be available to every pass that writes one. *Rejected:
  copying the helper into each pass, and rejected leaving the three unprotected, because the names
  they bind are exactly the ones a shader rename breaks with no diagnostic.*

## Non-goals

- `CompactInstancesPass`'s `stats` member is absent by build flag rather than by PSO variant; it
  converts to `SetIfValid` but gets no name validation, because the name is meant to be missing in a
  release build.
- `SceneBindings::BindSceneBuffers` keeps its own `gfatal` — it binds required buffers, not optional
  members, and is not what this changes.
- No change to what any shader reads or any pass writes. The bytes in every mirror are identical
  before and after.

## Acceptance

- `just run bgl_tests -- "[uniforms]"` — new cases pin that `SetIfValid` writes a declared member and
  that an undeclared one is left untouched without throwing, for both a value and a resource handle.
- `just test bgl` — the render goldens are unchanged, which is what proves the conversion moved no
  bytes; the four validated passes fail at `Init` if a bound name resolves in no variant.

## Commits

1. `docs(plans): plan the uniform accessor's guarded write` — this file.
2. `feat(bgl): a uniform's optional write is one expression` — `SetIfValid` on `AccessorBase`, its
   cases in `Uniforms_test.cpp`, and the `docs/uniforms.md` rows that describe the accessor.
   Gate: `just run bgl_tests -- "[uniforms]"`.
3. `refactor(bgl): bind optional uniforms through SetIfValid` — the ~50 sites across the five passes.
   Gate: `just test bgl` (goldens unchanged).
4. `refactor(bgl): every pass resolves its binder names against its PSOs` — `binder_names.h`, and the
   three passes that had no check. Gate: `just test bgl` (a stale name is a fatal at `Init`).
