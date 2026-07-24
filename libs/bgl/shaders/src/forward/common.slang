import types.SubmeshInstance;
import forward.ForwardData;
import idl.Constants;
import idl.PsoType;
import idl.Mesh;
import idl.Submesh;

public struct InstancedPayload
{
    public SubmeshInstance instance;
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    public float3 worldPos : POSITION0;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD0;
    public float4 tangent : TANGENT;
    public nointerpolation uint materialIndex : MATERIAL;

    // Which material buffer materialIndex addresses. The transparent pipelines are shared by both
    // material types, so their pixel shader cannot know it from the PSO the way the others do.
    public nointerpolation uint materialIsLoose : MATERIALTYPE;

    // Undivided clip position under this frame's and last frame's camera. Carried as a pair rather
    // than reusing SV_Position so both endpoints of the delta go through identical math, and so the
    // skinned path can substitute its own previous-frame position without touching the pixel stage.
    public float4 clip : CLIPPOSITION;
    public float4 prevClip : PREVCLIPPOSITION;
};

// Colour plus the screen-space velocity MRT slot. The transparent pipelines bind one render target
// and keep returning a bare float4: a blended surface has no single depth to reproject.
public struct ForwardPSOut
{
    public float4 color : SV_Target0;
    public float2 motion : SV_Target1;
};

// Where this pixel's surface sat last frame, as a UV displacement: history is sampled at
// `uv - motion`. NDC y runs opposite to UV y, hence the sign flip.
public float2 ComputeMotionVector(ForwardVSOut input)
{
    float2 ndc = input.clip.xy / input.clip.w;
    float2 prevNdc = input.prevClip.xy / input.prevClip.w;

    return (ndc - prevNdc) * float2(0.5, -0.5);
}

// A loose material stores its channels as routes rather than a baked triplet, so it reads from a
// different buffer. The PSO the instance resolved to is what says which.
//
// Written as comparisons, not a switch over the enum: the switch form silently misclassifies here
// in the mesh-shader stage, which the loose-vs-baked render test catches.
public bool IsLoosePso(PsoType pso)
{
    return pso == PsoType::kOpaque_StaticMesh_LoosePbr || pso == PsoType::kAlphaTest_StaticMesh_LoosePbr ||
           pso == PsoType::kTransparent_StaticMesh_LoosePbr || pso == PsoType::kTransparentOcclude_StaticMesh_LoosePbr;
}

public void ASBase(
    ConstantBuffer<ForwardData> sceneData,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    // The opaque path is bucketed by PSO, so its base is the exclusive prefix sum of prior PSO
    // counts. The transparent path is a separate depth-sorted list split by occlude class, so its
    // base is the partition's start. Both are GPU values the CPU cannot know at record time.
    uint base;
    if (sceneData.baseSource == cBaseSourceTransparentPartition)
    {
        base = sceneData.transparentPartitionBase[sceneData.partitionIndex];
    }
    else
    {
        base = (sceneData.psoIndex == 0u) ? 0u : sceneData.psoPrefixSum[sceneData.psoIndex - 1u];
    }

    uint drawInstanceIdx = sceneData.compactedInstances[base + gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = sceneData.meshBuffer.Get(instance.meshInstance);

    // Each instance is one submesh; dispatch only that submesh's meshlets.
    let submesh = sceneData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}
