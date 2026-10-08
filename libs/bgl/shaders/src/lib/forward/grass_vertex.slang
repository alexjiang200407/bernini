import lib.forward.common;

// What the grass stage hands its pixel programs: an ordinary material vertex (Surface()), and the
// terms that are the blade's geometry rather than its surface. Flat rather than holding a
// ForwardVSOut, because Metal refuses a mesh output with a nested struct.
//
// The normal is final: turned toward the camera, rounded across the width and blended toward the
// ground. What the UVs carry is docs/grass.md § Lighting.
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

    // How far above its blade's root this point stands, in world Y: what Blob Shadows lowers a
    // blade pixel by to shade it as its ground.
    public float rootHeight : GRASSROOTHEIGHT;

    public __init(ForwardVSOut surface, float4 tint, float rootHeight)
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
        this.rootHeight = rootHeight;
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

// The least height a blade pixel reports: one millimetre, well inside R16F's resolution near zero.
static const float cMinGrassRootHeight = 1e-3;

// A blade's targets: Forward's colour and velocity, and its height above its root, floored above
// zero so that the cleared zero is the one value that reads as no blade.
public struct GrassPSOut
{
    public float4 color : SV_Target0;
    public float4 motion : SV_Target1;
    public float rootHeight : SV_Target2;

    public __init(GrassVSOut input, float4 color)
    {
        this.color = color;
        motion = ComputeMotionVector(input.Surface());
        rootHeight = max(input.rootHeight, cMinGrassRootHeight);
    }
};
