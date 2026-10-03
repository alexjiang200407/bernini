import idl.ToonShadingRig;
import idl.ToonShadingRigBlock;
import idl.ToonShadingRigLimits;
import lib.types.ComputeBuffer;
import lib.types.RawBuffer;

// Loads a toon shading rig, one of its keys and an evaluated block out of one raw buffer, reported for a
// CPU comparison against the bytes it wrote: the fields at the ends of their structs and of their
// arrays, where a layout the C++ mirror disagrees with shows first.
struct Uniforms
{
    RawBuffer records;
    ComputeBuffer<float4> outValues;
    uint rigOffset;
    uint keyOffset;
    uint blockOffset;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let rig = gUniforms.records.Load<ToonShadingRig>(gUniforms.rigOffset);
    gUniforms.outValues[0] = rig.headToBone[2];
    gUniforms.outValues[1] = float4(float(rig.headBoneIndex), float(rig.editCount), float(rig.slotCount), rig.headRadius);
    gUniforms.outValues[2] = float4(rig.fadeStartPixels, rig.fadeEndPixels, rig.minElevation, rig.maxElevation);
    gUniforms.outValues[3] = float4(rig.maxAzimuth, rig.azimuthFadeStart, rig.azimuthFadeEnd, rig.azimuthFadeAmount);

    let last = rig.edits[cMaxToonShadingRigSlots - 1];
    gUniforms.outValues[4] = float4(float(rig.keys.GetStart()), float(last.firstKey), float(last.keyCount), last.keySharpness);
    gUniforms.outValues[5] = float4(
        float(last.flags),
        last.Has(ToonShadingRigEditFlag::kMirrored) ? 1.0 : 0.0,
        last.Has(ToonShadingRigEditFlag::kShadeOnly) ? 1.0 : 0.0,
        last.Has(ToonShadingRigEditFlag::kLightOnly) ? 1.0 : 0.0
    );

    let key = gUniforms.records.Load<ToonShadingRigKey>(gUniforms.keyOffset);
    gUniforms.outValues[6] = key.lightAndGain;
    gUniforms.outValues[7] = key.rotationRadiusSmoothing;

    let block = gUniforms.records.Load<ToonShadingRigBlock>(gUniforms.blockOffset);
    gUniforms.outValues[8] = block.headFromWorld[1];
    gUniforms.outValues[9] = block.faceLight;

    let slot = block.slots[cMaxToonShadingRigSlots - 1];
    gUniforms.outValues[10] = float4(float(block.slotCount), slot.bendBulgeRotation.w, slot.axisZAndSharpness.w, 0.0);
    gUniforms.outValues[11] = slot.radiusSmoothingMirror;
}
