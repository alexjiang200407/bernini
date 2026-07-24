# assetlib

assetlib is a static library that contains a set of asset-related utilities. These include:

- Parsing glTF (.glb / .gltf, via tinygltf) and retrieving all the assets — geometry, materials,
  textures, and the skin and animations, which are cooked into `.bskel` / `.banim` beside the mesh
- Provide shared structures that both bgl and editor can use to communicate

`.bmesh`, `.bskel` and `.banim` are one chunked container format, in `src/chunk_io.h`. A chunk is
addressed by id and an absent one is not an error, so adding data is a **minor** version bump and
leaves what is already on disk readable. `.bmaterial` is deliberately not one of them: it is a flat,
string-heavy stream with no bulk POD pools to chunk.