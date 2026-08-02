import types.SubmeshInstance;
import forward.ViewData;
import idl.PsoType;

// Declared here rather than beside the mesh shader that fills it: the pixel stage needs the jitter
// offsets to de-jitter its motion vector, and every pixel module imports this one.
public ConstantBuffer<ViewData> viewData;

public struct InstancedPayload
{
    public SubmeshInstance instance;
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    public float3 worldPos : POSITION;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD;
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
//
// Both clip positions carry the jitter of the frame that produced them, and each is removed against
// its own frame's offset -- a velocity is about the surface, and the sample pattern is not part of
// it. Both offsets are zero when the target has no temporal AA, which leaves this the plain
// difference it was.
public float2 ComputeMotionVector(ForwardVSOut input)
{
    float2 ndc = input.clip.xy / input.clip.w - viewData.jitter;
    float2 prevNdc = input.prevClip.xy / input.prevClip.w - viewData.prevJitter;

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
