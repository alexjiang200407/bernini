import idl.Constants;
import lib.data.SubmeshInstance;
import lib.math.motion_vector;

public struct InstancedPayload
{
    public SubmeshInstance instance;
}

// The static tier's payload: its instance, its submesh's meshlet count, and which of its meshlet
// groups survived the frustum -- a bit per group of cMeshletsPerGroup meshlets, and before each mask
// word how many survivors the earlier words hold, two 16-bit counts a word.
public struct CulledPayload
{
    public SubmeshInstance instance;
    public uint meshletCount;
    public uint survivorMask[cMaxCompactedGroups / 32];
    public uint survivorsBefore[cMaxCompactedGroups / 64];
}

// Groups the submesh is cut into. The last one is short unless cMeshletsPerGroup divides the count,
// and the mesh groups standing for its missing meshlets emit nothing.
public uint MeshletGroupCount(uint meshletCount)
{
    return (meshletCount + cMeshletsPerGroup - 1u) / cMeshletsPerGroup;
}

// The meshlet group the `ordinal`-th survivor is, in group order.
// @pre `ordinal` is below the payload's survivor count.
public uint CulledGroupIndex(CulledPayload payload, uint ordinal)
{
    // The last mask word whose earlier survivors number no more than `ordinal` holds it.
    uint lo = 0u;
    uint hi = (MeshletGroupCount(payload.meshletCount) + 31u) / 32u;
    while (hi - lo > 1u)
    {
        let mid = (lo + hi) / 2u;
        let before = (payload.survivorsBefore[mid / 2u] >> ((mid & 1u) * 16u)) & 0xFFFFu;
        if (before <= ordinal)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }

    uint mask = payload.survivorMask[lo];
    let before = (payload.survivorsBefore[lo / 2u] >> ((lo & 1u) * 16u)) & 0xFFFFu;
    for (uint skip = ordinal - before; skip > 0u; --skip)
    {
        mask &= mask - 1u;
    }
    return lo * 32u + firstbitlow(mask);
}

// The meshlet mesh group `gid` of the static tier draws: the group it falls in is the (gid /
// cMeshletsPerGroup)-th survivor, and its place within that group is the remainder. Past the
// submesh's meshlet count where the last group is short.
public uint CulledMeshletIndex(CulledPayload payload, uint gid)
{
    return CulledGroupIndex(payload, gid / cMeshletsPerGroup) * cMeshletsPerGroup + (gid % cMeshletsPerGroup);
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    public float3 worldPos : POSITION;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD;
    // The second UV set, or cNoUv1 on a mesh without one; see HasUv1. Not TEXCOORD1: on Metal,
    // Slang names a numbered semantic differently as a mesh output than as a fragment input.
    public float2 uv1 : SECONDUV;
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
