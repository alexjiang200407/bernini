# Animal avatar parts

`Bear.bavatar`, `Owl.bavatar` and `Cat.bavatar` map the corresponding `glb-specular` animal rigs.
Copy the `parts` object into the avatar paired with your imported skeleton. Keep any existing
`legs` and `plant` settings; these examples use empty `legs` and do not enable foot planting.
The bird's `UpperArm`/`Forearm`/`Hand` chain is labelled as a wing. `tongue` demonstrates a custom
part name; the other names follow the conventions in [skinning.md](docs/skinning.md#named-body-parts).

Each `.skeleton.json` is a portable test fixture extracted from that GLB's skin: one
`[bone name, parent joint index]` per joint, with `-1` for a root. It preserves the complete joint
hierarchy and source joint order, but contains no geometry, transforms or animation data. The
tests build a glTF skin from it and import twice with different joint orders, checking that the
same JSON still resolves to the same named chains. These files are test inputs, not engine assets.

Run the portable gates from the repository root:

```sh
just run assetlib_tests -- "[parts]~[.avatar-source]"
```

To validate the examples against the original GLBs as well, supply their directory explicitly:

```sh
BERNINI_AVATAR_SOURCE_ROOT="/absolute/path/to/glb-specular" just run assetlib_tests -- "[.avatar-source]"
```

This opt-in test imports Bear, Owl and Cat through the real GLB loader and checks that every
example part resolves without a description error. The external GLBs are not required by the
ordinary suite.
