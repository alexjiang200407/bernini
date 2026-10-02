import idl.Constants;
import lib.data.SubmeshInstance;
import lib.math.motion_vector;

public struct InstancedPayload
{
    public SubmeshInstance instance;

    // The place in the geom's submesh range of the level this entry draws, and its dissolve code
    // (DissolveVSOut.lodDissolve).
    public uint submeshEntry;
    public float lodDissolve;
}

// Survivor groups one CulledPayload::survivorMask word holds, a bit each.
public static const uint cSurvivorMaskBits = 32u;

// The static tier's payload: its instance, its submesh's meshlet count, and which of its meshlet
// groups survived the frustum -- a bit per group of cMeshletsPerGroup meshlets, and before each mask
// word how many survivors the earlier words hold, two 16-bit counts a word.
public struct CulledPayload
{
    public SubmeshInstance instance;
    public uint submeshEntry;
    public float lodDissolve;
    public uint meshletCount;
    public uint survivorMask[cMaxCompactedGroups / cSurvivorMaskBits];
    public uint survivorsBefore[cMaxCompactedGroups / (2u * cSurvivorMaskBits)];
}

// Groups the submesh is cut into. The last one is short unless cMeshletsPerGroup divides the count,
// and a dispatch launches no mesh group for its missing meshlets (CullMeshlets).
public uint MeshletGroupCount(uint meshletCount)
{
    return (meshletCount + cMeshletsPerGroup - 1u) / cMeshletsPerGroup;
}

// The meshlet group the `ordinal`-th survivor is, in group order.
// @pre `ordinal` is below the payload's survivor count.
public uint CulledGroupIndex(CulledPayload payload, uint ordinal)
{
    // The last mask word whose earlier survivors number no more than `ordinal` holds it. Counted to
    // a constant number of halvings for the same reason the bit loop below is counted to the word's
    // width: eight exhausts the cMaxCompactedGroups / cSurvivorMaskBits words a payload can carry.
    uint lo = 0u;
    uint hi =
        min((MeshletGroupCount(payload.meshletCount) + cSurvivorMaskBits - 1u) / cSurvivorMaskBits,
            cMaxCompactedGroups / cSurvivorMaskBits);
    for (uint step = 0u; step < 8u; ++step)
    {
        if (hi - lo <= 1u)
        {
            break;
        }

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
    let skip = ordinal - before;

    // Counted to the word's width, never to `skip`, though the precondition puts `skip` below it.
    // Bounded by `skip` alone this loop does not terminate on D3D12 -- the group spins, the queue
    // wedges and TDR removes the device -- and it does so with the precondition holding, checked.
    // What it turns on is the trip count being a read of payload memory; the width is a constant.
    for (uint cleared = 0u; cleared < cSurvivorMaskBits; ++cleared)
    {
        if (cleared >= skip)
        {
            break;
        }
        mask &= mask - 1u;
    }
    return lo * cSurvivorMaskBits + firstbitlow(mask);
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

// What a draw bucket's dissolve pipelines rasterize: an ordinary material vertex (Surface()), and
// its placement's share of the level this draw is -- in [0, 1) its current level keeps a pixel whose
// dissolve hash is below it, in [1, 2) its outgoing level keeps one at or above it less 1: the
// complement, so the two levels never both cover a pixel. Only a placement dissolving draws through
// these pipelines, so one at rest carries no code and runs no discard. Flat rather than holding a
// ForwardVSOut, because Metal refuses a mesh output with a nested struct.
public struct DissolveVSOut
{
    public float4 position : SV_Position;
    public float3 worldPos : POSITION;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD;
    public float2 uv1 : SECONDUV;
    public float4 tangent : TANGENT;
    public nointerpolation uint materialOffset : MATERIAL;
    public float4 clip : CLIPPOSITION;
    public float4 prevClip : PREVCLIPPOSITION;
    public float4 cameraPrevClip : CAMERAPREVCLIPPOSITION;

    public nointerpolation float lodDissolve : LODDISSOLVE;

    public __init(ForwardVSOut surface, float lodDissolve)
    {
        position = surface.position;
        worldPos = surface.worldPos;
        normal = surface.normal;
        uv = surface.uv;
        uv1 = surface.uv1;
        tangent = surface.tangent;
        materialOffset = surface.materialOffset;
        clip = surface.clip;
        prevClip = surface.prevClip;
        cameraPrevClip = surface.cameraPrevClip;
        this.lodDissolve = lodDissolve;
    }

    public ForwardVSOut Surface()
    {
        ForwardVSOut surface;
        surface.position = position;
        surface.worldPos = worldPos;
        surface.normal = normal;
        surface.uv = uv;
        surface.uv1 = uv1;
        surface.tangent = tangent;
        surface.materialOffset = materialOffset;
        surface.clip = clip;
        surface.prevClip = prevClip;
        surface.cameraPrevClip = cameraPrevClip;
        return surface;
    }
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
