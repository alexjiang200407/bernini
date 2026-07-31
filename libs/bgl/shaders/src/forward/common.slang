import types.SubmeshInstance;
import idl.PsoType;

public struct InstancedPayload
{
    public SubmeshInstance instance;
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    // No semantic here may end in a digit: Slang's MSL emits a mesh output's verbatim but a
    // fragment input's as NAME for index 0 and NAME_<n> otherwise, so the two agree only when
    // there is no index. Metal's stage linkage rejects the mismatch; D3D12 is unaffected.
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
