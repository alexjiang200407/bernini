import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.GrassShading;
import lib.forward.grass_vertex;
import lib.forward.MaterialData;

// A grass blade over a baked triplet PBR material.
[shader("pixel")]
GrassPSOut PSMain(GrassVSOut input)
{
    return GrassPSOut(input, materialData.ShadeGrass<PbrMaterialRecord>(input));
}
