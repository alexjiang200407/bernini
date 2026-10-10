import bgl.PbrSurface;
import idl.Constants;
import idl.Geom;
import idl.Impostor;
import idl.InstanceLod;
import idl.LodDrawMode;
import idl.MeshInstance;
import idl.RawArena;
import lib.data.SubmeshInstance;
import lib.forward.common;
import lib.forward.ExpansionData;
import lib.forward.ForwardData;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.mesh_stage;
import lib.types.RawHandleArena;

// The impostor stage: a placement past its geom's last level of detail drawn as one quad facing the
// camera across the geom's bake sphere, its pixels the three atlas frames nearest the view blended,
// and lit through the same ShadeSurface the mesh is. The atlas layout is assetlib::MeshImpostor's.

public struct ImpostorData
{
    // Every geom's Impostor record, and the typed view that makes textures of the handles it holds.
    public RawHandleArena<Texture2D.Handle> impostors;
};

public ConstantBuffer<ImpostorData> impostorData;

struct ImpostorPayload
{
    uint compacted;
    float lodDissolve;
};

groupshared ImpostorPayload gImpostorPayload;

struct ImpostorVSOut
{
    float4 position : SV_Position;
    float3 worldPos : POSITION;
    float4 clip : CLIPPOSITION;
    float4 prevClip : PREVCLIPPOSITION;
    float4 cameraPrevClip : CAMERAPREVCLIPPOSITION;

    // Where on the quad, in units of the sphere's radius: (-1, -1) at its lower left.
    float2 corner : IMPOSTORCORNER;

    nointerpolation uint compacted : IMPOSTORENTRY;
    nointerpolation float lodDissolve : LODDISSOLVE;
};

// The direction toward the viewer frame (x, y) was baked from: assetlib::impostorFrameDirection.
float3 FrameDirection(float2 grid, uint framesPerSide)
{
    let e = grid / float(framesPerSide - 1u) * 2.0 - 1.0;
    let t = float2(e.x + e.y, e.x - e.y) * 0.5;
    return normalize(float3(t.x, 1.0 - abs(t.x) - abs(t.y), t.y));
}

// The inverse, onto the grid in frames; a direction below the horizon is taken onto it.
float2 FrameGrid(float3 toViewer, uint framesPerSide)
{
    var v = float3(toViewer.x, max(toViewer.y, 0.0), toViewer.z);
    v /= abs(v.x) + abs(v.z) + v.y;
    let e = float2(v.x + v.z, v.x - v.z);
    return (e * 0.5 + 0.5) * float(framesPerSide - 1u);
}

// A frame's right and up, as the bake laid it out (assetlib's BasisOf).
void FrameBasis(float3 toViewer, out float3 right, out float3 up)
{
    right = abs(toViewer.y) > 0.9999 ? float3(1.0, 0.0, 0.0) : normalize(cross(float3(0.0, 1.0, 0.0), toViewer));
    up = cross(toViewer, right);
}

// A direction in the placement's local space: the transpose of its rotation, and its scale divided
// back out by the normalize -- placements are TRS with uniform scale.
float3 ToLocal(MeshInstance mesh, float3 d)
{
    return normalize(d.x * mesh.transform[0].xyz + d.y * mesh.transform[1].xyz + d.z * mesh.transform[2].xyz);
}

struct Placement
{
    MeshInstance mesh;
    Geom geom;
    Impostor impostor;

    // Where the record's payload starts in the arena, which is how its handles are found.
    uint handles;
};

Placement LoadPlacement(uint compacted)
{
    let instance = forwardData.instanceBuffer.Get(compacted & ~cOutgoingDrawBit);
    Placement p;
    p.mesh = forwardData.meshBuffer.Get(instance.meshInstance);
    p.geom = forwardData.geomBuffer.Get(p.mesh.geom);
    p.impostor = impostorData.impostors.LoadRecordAs<Impostor>(p.geom.impostor);
    p.handles = p.geom.impostor.GetByteOffset() + cRawPayloadOffset;
    return p;
}

// The impostor's share of a dissolve between it and the placement's last level: the current tier's
// code when the placement is fading into its impostor, the outgoing one's when out of it.
float ImpostorDissolve(uint compacted)
{
    let instance = forwardData.instanceBuffer.Get(compacted & ~cOutgoingDrawBit);
    let word = expansionData.instanceLod[instance.meshInstance.GetOffset()];
    if (expansionData.lodDrawMode != LodDrawMode::kDissolve || word.Unset() || !word.Fading())
    {
        return 1.0;
    }
    let geom = forwardData.geomBuffer.Get(forwardData.meshBuffer.Get(instance.meshInstance).geom);
    return uint(word.Level()) >= geom.submeshes.lodCount ? word.Fade() : 1.0 + word.Fade();
}

[numthreads(1, 1, 1)]
[shader("amplification")]
void ASMain(uint gid: SV_GroupID)
{
    let compacted = expansionData.compactedInstances[ResolveInstanceBase() + gid];
    gImpostorPayload.compacted = compacted;
    gImpostorPayload.lodDissolve = ImpostorDissolve(compacted);
    DispatchMesh(1, 1, 1, gImpostorPayload);
}

ImpostorVSOut QuadCorner(ImpostorPayload payload, uint v)
{
    let p = LoadPlacement(payload.compacted);
    let sphere = p.mesh.TransformSphere(p.impostor.sphere);
    let toCamera = normalize(expansionData.cullView[0].cameraPosAndPixelsPerUnit.xyz - sphere.xyz);

    float3 right;
    float3 up;
    FrameBasis(toCamera, right, up);

    let corner = float2((v & 1u) != 0u ? 1.0 : -1.0, (v & 2u) != 0u ? 1.0 : -1.0);
    let world = sphere.xyz + (corner.x * right + corner.y * up) * sphere.w;

    // The quad turns with the camera, so only the placement's own motion is carried back a frame.
    let moved = p.mesh.PrevTransformPoint(p.impostor.sphere.xyz) - p.mesh.TransformPoint(p.impostor.sphere.xyz);

    ForwardVSOut projected;
    ProjectVertex(projected, float4(world, 1.0), float4(world + moved, 1.0));

    ImpostorVSOut out;
    out.position = projected.position;
    out.worldPos = projected.worldPos;
    out.clip = projected.clip;
    out.prevClip = projected.prevClip;
    out.cameraPrevClip = projected.cameraPrevClip;
    out.corner = corner;
    out.compacted = payload.compacted;
    out.lodDissolve = payload.lodDissolve;
    return out;
}

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gtid: SV_GroupThreadID,
    payload ImpostorPayload payload,
    OutputVertices<ImpostorVSOut, 4> verts,
    OutputIndices<uint3, 2> tris
)
{
    SetMeshOutputCounts(4, 2);
    verts[gtid] = QuadCorner(payload, gtid);
    if (gtid < 2u)
    {
        tris[gtid] = gtid == 0u ? uint3(0, 1, 3) : uint3(0, 3, 2);
    }
}

// The same quad for the bucket's dissolve lane; the vertex already carries the dissolve code.
[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSDissolve(
    uint gtid: SV_GroupThreadID,
    payload ImpostorPayload payload,
    OutputVertices<ImpostorVSOut, 4> verts,
    OutputIndices<uint3, 2> tris
)
{
    SetMeshOutputCounts(4, 2);
    verts[gtid] = QuadCorner(payload, gtid);
    if (gtid < 2u)
    {
        tris[gtid] = gtid == 0u ? uint3(0, 1, 3) : uint3(0, 3, 2);
    }
}

struct ImpostorSample
{
    float3 albedo;
    float coverage;
    float3 normal; // local space, unnormalised
    float depth;   // toward the viewer across the sphere, in radii
};

// One frame's surface where the pixel's point on the quad projects into it along the frame's view.
ImpostorSample SampleFrame(Placement p, uint2 frame, float3 local, float weight)
{
    let n = p.impostor.framesPerSide;
    float3 right;
    float3 up;
    FrameBasis(FrameDirection(float2(frame), n), right, up);

    let inFrame = float2(dot(local, right) * 0.5 + 0.5, 0.5 - dot(local, up) * 0.5);
    let uv = (float2(frame) + saturate(inFrame)) / float(n);

    let albedo = impostorData.impostors.GetHandleAt(p.handles, 0).SampleLevel(materialData.linearClampSampler, uv, 0.0);
    let normalDepth = impostorData.impostors.GetHandleAt(p.handles, 1).SampleLevel(materialData.linearClampSampler, uv, 0.0);

    // Off the frame is off the mesh: what saturate pulled back in is another texel's surface.
    let inside = all(inFrame == saturate(inFrame)) ? 1.0 : 0.0;
    let w = weight * albedo.a * inside;

    ImpostorSample s;
    s.albedo = albedo.rgb * w;
    s.coverage = w;
    s.normal = (normalDepth.rgb * 2.0 - 1.0) * w;
    s.depth = (normalDepth.a * 2.0 - 1.0) * w;
    return s;
}

struct ImpostorPixel
{
    ForwardPSOut output;
    float depth : SV_Depth;
};

ImpostorPixel Program(ImpostorVSOut input)
{
    let p = LoadPlacement(input.compacted);
    let sphere = p.mesh.TransformSphere(p.impostor.sphere);
    let toCamera = normalize(expansionData.cullView[0].cameraPosAndPixelsPerUnit.xyz - sphere.xyz);

    float3 right;
    float3 up;
    FrameBasis(toCamera, right, up);
    let local = ToLocal(p.mesh, right) * input.corner.x + ToLocal(p.mesh, up) * input.corner.y;

    // The three frames around the view on the grid, by their barycentric weights.
    let n = p.impostor.framesPerSide;
    let grid = FrameGrid(ToLocal(p.mesh, toCamera), n);
    let base = min(uint2(floor(grid)), uint2(n - 2u, n - 2u));
    let f = grid - float2(base);
    ImpostorSample a, b, c;
    if (f.x + f.y < 1.0)
    {
        a = SampleFrame(p, base, local, 1.0 - f.x - f.y);
        b = SampleFrame(p, base + uint2(1, 0), local, f.x);
        c = SampleFrame(p, base + uint2(0, 1), local, f.y);
    }
    else
    {
        a = SampleFrame(p, base + uint2(1, 1), local, f.x + f.y - 1.0);
        b = SampleFrame(p, base + uint2(1, 0), local, 1.0 - f.y);
        c = SampleFrame(p, base + uint2(0, 1), local, 1.0 - f.x);
    }

    let coverage = a.coverage + b.coverage + c.coverage;
    if (coverage < 0.5)
    {
        discard;
    }

    let albedo = (a.albedo + b.albedo + c.albedo) / coverage;
    let normal = normalize(p.mesh.TransformDirection(a.normal + b.normal + c.normal));
    let depth = (a.depth + b.depth + c.depth) / coverage;

    // The surface the frames saw, moved off the quad toward the viewer by its baked depth.
    let surfacePos = input.worldPos + toCamera * depth * sphere.w;

    ForwardVSOut surface;
    surface.position = input.position;
    surface.worldPos = surfacePos;
    surface.normal = normal;
    surface.uv = float2(0.0, 0.0);
    surface.uv1 = float2(0.0, 0.0);
    surface.tangent = float4(normalize(cross(abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), normal)), 1.0);
    surface.materialOffset = 0u;
    surface.clip = input.clip;
    surface.prevClip = input.prevClip;
    surface.cameraPrevClip = input.cameraPrevClip;

    // The atlas bakes base colour alone, so the rest is a matte dielectric's: PbrSurface()'s.
    var lit = PbrSurface();
    lit.baseColor = float4(albedo, 1.0);

    ImpostorPixel pixel;
    pixel.output.color = float4(materialData.ShadeSurface(surface, lit, true), 1.0);
    pixel.output.motion = ComputeMotionVector(surface);

    let clip = mul(viewData.viewProj, float4(surfacePos, 1.0));
    pixel.depth = clip.z / clip.w;
    return pixel;
}

[shader("pixel")]
ImpostorPixel PSMain(ImpostorVSOut input)
{
    return Program(input);
}

[shader("pixel")]
ImpostorPixel PSDissolve(ImpostorVSOut input)
{
    let hash = LodDissolveHash(input.position.xy);
    let keeps = input.lodDissolve < 1.0 ? hash < input.lodDissolve : hash >= input.lodDissolve - 1.0;
    if (!keeps)
    {
        discard;
    }
    var pixel = Program(input);
    pixel.output = MarkDissolvedLod(pixel.output);
    return pixel;
}
