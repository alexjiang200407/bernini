import lib.forward.common;

// What the grass stage hands its pixel programs: an ordinary material vertex (Surface()), and the
// terms that are the blade's geometry rather than its surface. Flat rather than holding a
// ForwardVSOut, because Metal refuses a mesh output with a nested struct.
//
// uv runs across the blade (x) and root to tip (y). uv1 is never a second UV set, which is why a grass
// program reads no geometry occlusion: x is the blade's own random, and y the entry of its look in
// grassData.looks -- constant along the blade, where a flat attribute would cost the pass a third
// more on Apple silicon. The normal is final: turned toward the camera, rounded across the width
// and blended toward the ground.
public struct GrassVSOut
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

    // The base colour's multiplier -- the look's tints, the blade's variation and its clump's colour
    // -- and the root-to-tip occlusion in a.
    public float4 tint : GRASSTINT;

    public __init(ForwardVSOut surface, float4 tint)
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
        this.tint = tint;
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
