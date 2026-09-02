// WGSL-lowering driver for lib.anim.skinning, over a palette that is one storage buffer.
import idl.Clip;
import idl.IBonePalette;
import idl.Rig;
import lib.anim.skinning;

RWStructuredBuffer<float4> gPalette;
RWStructuredBuffer<float4> gOut;

struct StoragePalette : IBonePalette
{
    float4 GetPalette(uint slot) { return gPalette[slot]; }
};

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    StoragePalette palette;
    Rig rig = {};
    Clip clip = {};
    uint lower;
    uint upper;
    float weight;
    TableFrameSlots(rig, clip, float(id.x), lower, upper, weight);
    let joints = float4(0.0, 1.0, 2.0, 3.0);
    let weights = float4(0.25, 0.25, 0.25, 0.25);
    let skin = SkinAt(palette, true, lower, upper, weight, joints, weights);
    gOut[0] = mul(skin, float4(1.0, 0.0, 0.0, 1.0)) + mul(SkinMatrix(palette, lower, joints, weights), float4(0.0, 1.0, 0.0, 1.0));
}
