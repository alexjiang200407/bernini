import idl.AutoPosedInstance;
import idl.BlobShadow;
import idl.CullView;
import idl.DominantFrames;
import idl.InstancePose;
import idl.Rig;
import idl.SkinnedAutoState;
import idl.SkinnedTableState;
import lib.types.ComputeBuffer;
import lib.types.RawBuffer;

// Loads out of one raw buffer, reported for a CPU comparison against the bytes it wrote.
//
// The record loads answer whether a raw Load<T> lays T out the way bgpu_idlgen's C++ mirror
// asserts: SkinnedTableState is a real playback record of packed scalars, CullView adds a matrix, a
// fixed array and scalars trailing a float4, which are where a target's own packing rules would
// show up first, and SkinnedAutoState an array of structs with a field after it.
//
// The loose loads answer the other half: a vertex attribute is only ever 4-byte aligned, so both
// come from an address that is 4-aligned and deliberately not 16-aligned.
struct Uniforms
{
    RawBuffer records;
    ComputeBuffer<float4> outValues;
    uint stateOffset;
    uint viewOffset;
    uint vertexOffset;
    uint autoOffset;
    uint rigOffset;
    uint blobOffset;
    uint poseOffset;
    uint posedOffset;
    uint framesOffset;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let state = gUniforms.records.Load<SkinnedTableState>(gUniforms.stateOffset);
    gUniforms.outValues[0] =
        float4(float(state.playback.rig.GetOffset()), float(state.playback.clip), state.playback.phase, state.playback.rate);

    let view = gUniforms.records.Load<CullView>(gUniforms.viewOffset);
    gUniforms.outValues[1] = view.frustumPlanes[3];
    gUniforms.outValues[2] = view.viewProj[2];
    gUniforms.outValues[3] = view.cameraPosAndPixelsPerUnit;
    gUniforms.outValues[4] = float4(view.lodPixelScale, float(view.lodForcedLevel), view.lodFadeStep, view.posePixels);
    gUniforms.outValues[7] = float4(float(view.poseBudget), float(view.poseForced), 0.0, 0.0);

    gUniforms.outValues[5] = gUniforms.records.Load<float4>(gUniforms.vertexOffset);
    gUniforms.outValues[6] = float4(gUniforms.records.Load<float3>(gUniforms.vertexOffset + 16u), 0.0);

    let autoState = gUniforms.records.Load<SkinnedAutoState>(gUniforms.autoOffset);
    gUniforms.outValues[8] = float4(
        float(autoState.rig.GetOffset()),
        float(autoState.slots[3].nodeIndex),
        autoState.slots[3].paramEnd,
        float(autoState.footIK.GetStart())
    );

    let rig = gUniforms.records.Load<Rig>(gUniforms.rigOffset);
    let blob = gUniforms.records.Load<BlobShadow>(gUniforms.blobOffset);
    let pose = gUniforms.records.Load<InstancePose>(gUniforms.poseOffset);
    gUniforms.outValues[9] =
        float4(float(rig.boneAnimTable.GetStart()), float(rig.tableSoles.GetStart()), float(blob.leg), float(pose.palette.GetStart()));

    let posed = gUniforms.records.Load<AutoPosedInstance>(gUniforms.posedOffset);
    gUniforms.outValues[10] =
        float4(float(posed.meshInstanceIndex), float(posed.footIK.GetStart()), float(posed.palette.GetStart()), posed.ikScale);

    let dominant = gUniforms.records.Load<DominantFrames>(gUniforms.framesOffset);
    gUniforms.outValues[11] =
        float4(float(dominant.lowerFrame), dominant.upperWeight, float(dominant.prevUpperFrame), dominant.prevUpperWeight);
}
