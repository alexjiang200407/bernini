import lib.data.SubmeshInstance;
import lib.math.motion_vector;

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
    // A byte offset into the material arena, naming the record's header. The record says which
    // kind it is, which is what the transparent pipeline reads: it is shared by both, so it cannot
    // know from the PSO the way the others do.
    public nointerpolation uint materialOffset : MATERIAL;

    // Undivided clip position under this frame's and last frame's camera, with each frame's jitter
    // already removed -- SV_Position keeps its offset, these do not, because a velocity is about the
    // surface and the sample pattern is not part of it. Carried as a pair rather than reusing
    // SV_Position so both endpoints of the delta go through identical math, and so the skinned path
    // can substitute its own previous-frame position without touching the pixel stage.
    public float4 clip : CLIPPOSITION;
    public float4 prevClip : PREVCLIPPOSITION;

    // This frame's world position under last frame's camera: where the surface would have been had
    // only the camera moved. Projected by the same math as prevClip, so a surface that did not move
    // carries the same value bit for bit.
    public float4 cameraPrevClip : CAMERAPREVCLIPPOSITION;
};

// Colour plus the screen-space velocity MRT slot. The transparent pipelines bind one render target
// and keep returning a bare float4: a blended surface has no single depth to reproject.
public struct ForwardPSOut
{
    public float4 color : SV_Target0;
    public float4 motion : SV_Target1;
};

// Where this pixel's surface sat last frame (xy), and the part of that it moved on its own (zw) --
// exactly zero for a surface nothing moved. The arithmetic is lib.math.motion_vector's.
public float4 ComputeMotionVector(ForwardVSOut input)
{
    float2 motion = ComputeMotionVector(input.clip, input.prevClip);
    float2 cameraMotion = ComputeMotionVector(input.clip, input.cameraPrevClip);
    return float4(motion, motion - cameraMotion);
}
