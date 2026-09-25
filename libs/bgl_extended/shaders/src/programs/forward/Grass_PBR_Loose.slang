import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.GrassShading;
import lib.forward.grass_vertex;
import lib.forward.MaterialData;

// A grass blade over an unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(GrassVSOut input)
{
    return materialData.GrassProgram<LoosePbrMaterialRecord>(input);
}
