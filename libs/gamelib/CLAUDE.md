# gamelib

gamelib provides game-related abstractions built on top of the renderer and the asset layer. It is the
**seam**: the only library allowed to link both `bgl` and `assetlib`.

- CMake target: `gamelib` (static). CMake: `./CMakeLists.txt`
- Namespace: `game`
- Verification: `gamelib_tests`

## Why it exists

The two libraries below it are deliberately kept apart:

- `bgl` links `assetlib_structs` (POD headers) but **never** `assetlib`. Image decoding lives in the
  asset library; graphics code stays codec-free and consumes decoded `ImageData` through
  `IScene::AddTextureAsset`.
- `assetlib` **never** links `bgl`. It is the offline cook library, and `assetlib_cli` uses it — a
  command-line baker must not pull in a D3D12 renderer.

Anything that needs both — "read this `.bmaterial` off disk and give me a `MaterialHandle`" — belongs
here, not in either of them.

## Contents

- `AssetFactory` — constructed with an `IScene` and the project's **Data directory**. Every asset
  reference Bernini stores is a path relative to that root, so it is supplied once at construction
  rather than threaded through every call. The factory owns identity: a path maps to one texture upload
  and one material, however many times it is asked for. Nothing is evicted; its lifetime is the scene's.
