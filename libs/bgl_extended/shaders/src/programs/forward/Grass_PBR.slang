import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.GrassShading;
import lib.forward.grass_vertex;
import lib.forward.MaterialData;

// A grass blade over a baked triplet PBR material.
[shader("pixel")]
ForwardPSOut PSMain(GrassVSOut input)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGrass<PbrMaterialRecord>(input);
    output.motion = ComputeMotionVector(input.Surface());
    return output;
}
