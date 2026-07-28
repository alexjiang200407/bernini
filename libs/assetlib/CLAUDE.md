# assetlib

assetlib is a static library that contains a set of asset-related utilities. These include:

- Parsing glTF (.glb / .gltf, via tinygltf) and retrieving all the assets
- Provide shared structures that both bgl and editor can use to communicate

## Headers forward declare

A header forward declares the `assetlib`-namespace types it names — `BMesh`, `BMaterial`,
`ImageData`, `imp::BMeshImport` — instead of including `assetlib_structs`. Include the definition
only where one is genuinely required: `benv_io.h` includes `ImageData.h` because `EnvironmentMaps`
holds three of them by value, and a reference or a by-value return does not.

Types from another namespace (`core::`, `std::`) are included as normal. Forward declaring across a
namespace boundary buys nothing and couples this library to how that one spells its templates.

The corollary is that a `.cpp` includes the structs it uses. Nothing picks a definition up
transitively through an `assetlib/` header, so an include here names what the file actually needs.