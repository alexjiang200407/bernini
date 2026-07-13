# Features
Add Cancel for Asset imports
If exception occurs should show popup. Import mesh, texture, material, OS errors cant overwrite etc
Delete Material / Texture / Mesh. Should check for no references first
Material should show current baked textures if any
Show thumbnail for meshes

# Bugs
Sometimes, when exiting, refuses to exist
Scrolling in the material node editor will reduce frame rate for the material preview.
Currently if two submeshes use the same material, and we update the material for 1 it doesn't update the material for the other submesh.
  Still open. The material editor gives every submesh its own graph and its own preview material, even when two of them name the same .bmaterial, so editing one cannot reach the other. The fix is to key the preview material by path (what gamelib's AssetManager already does) rather than by submesh -- the editor does not link gamelib.