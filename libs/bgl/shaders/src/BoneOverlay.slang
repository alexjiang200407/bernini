import idl.Constants;
import idl.Mesh;
import idl.SkinnedBone;
import idl.SkinnedGeom;
import idl.SkinnedState;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.RangeBuffer;
import types.UploadBuffer;

// One octahedron per bone, spanning the bone to its parent -- the DCC shape, so length and roll are
// readable at a glance. One mesh group draws one bone: x indexes the bone, y the posed instance.
//
// The pose comes back out of the palette the pose pass wrote rather than from a second reading of the
// clip, so what is drawn is the pose that skins the mesh beside it. A palette entry is
// model x inverseBind, so undoing the inverse bind is what recovers the bone itself.

struct Uniforms
{
    // Parallel lists, both dense: the state to read a palette from, and the placement whose world
    // transform carries it out of model space. See SceneView::RebuildPosedList.
    UploadBuffer<uint> posedInstances;
    UploadBuffer<uint> posedMeshes;

    EntryBuffer<Mesh> meshes;
    EntryBuffer<SkinnedState> skinnedStates;
    EntryBuffer<SkinnedGeom> skinnedGeoms;

    RangeBuffer<SkinnedBone> boneBuffer;

    ComputeBuffer<float4> bonePalettes;

    float4x4 viewProj;

    // A bone with no parent, and a bone sitting on its parent, have no length to be drawn along.
    // They get a marker of this size in world units instead, so a root is visible rather than absent.
    float rootSize;

    float3 color;
};

ConstantBuffer<Uniforms> gUniforms;

// Indistinguishable from zero in float, rather than tuned thresholds: a basis whose determinant
// falls below the first has no usable inverse, and a vector whose squared length falls below the
// second has no direction to normalize.
static const float c_MinDeterminant = 1e-12;
static const float c_MinLengthSq = 1e-12;

// World units. A bone shorter than this has no direction to be drawn along -- a root, or one
// standing on its parent -- so it gets a marker instead of a solid.
static const float c_MinBoneLength = 1e-6;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

// The inverse of a rotation-scale-shear basis, by adjugate. The rows of the result are built as
// columns, hence the transpose.
float3x3 InverseBasis(float3x3 m)
{
    float3 a = cross(m[1], m[2]);
    float3 b = cross(m[2], m[0]);
    float3 c = cross(m[0], m[1]);

    float det = dot(m[0], a);

    // A bone scaled flat on any axis has no inverse. Any finite answer will be wrong, so this picks
    // the one that stays finite: the NaN it replaces would reach the rasteriser as a triangle
    // covering the screen.
    if (abs(det) < c_MinDeterminant)
    {
        return float3x3(1.0);
    }

    float invDet = 1.0 / det;
    return transpose(float3x3(a * invDet, b * invDet, c * invDet));
}

// The inverse of an affine transform whose translation is the .w of each row -- the convention the
// pose pass builds and the palette carries.
float4x4 AffineInverse(float4x4 m)
{
    float3x3 basis = InverseBasis(float3x3(m[0].xyz, m[1].xyz, m[2].xyz));
    float3 translation = -mul(basis, float3(m[0].w, m[1].w, m[2].w));

    return float4x4(
        float4(basis[0], translation.x), float4(basis[1], translation.y), float4(basis[2], translation.z), float4(0.0, 0.0, 0.0, 1.0)
    );
}

// Where bone `bone` of this instance stands, in world space. `record` is that bone's own, which the
// caller has already read.
float4x4 BoneWorld(Mesh mesh, SkinnedState state, SkinnedBone record, uint bone)
{
    let row = state.palette.GetStart() + bone * cFloat4sPerBone;

    // The fourth row of an affine transform is always (0,0,0,1), which is why the pose pass did not
    // write it.
    float4x4 skin = float4x4(
        gUniforms.bonePalettes[row + 0], gUniforms.bonePalettes[row + 1], gUniforms.bonePalettes[row + 2], float4(0.0, 0.0, 0.0, 1.0)
    );

    return mul(mesh.transform, mul(skin, AffineInverse(record.inverseBind)));
}

float3 TranslationOf(float4x4 m)
{
    return float3(m[0].w, m[1].w, m[2].w);
}

[shader("mesh")]
[numthreads(8, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint3 groupId: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 6> verts, OutputIndices<uint3, 8> tris)
{
    let instanceIdx = groupId.y;
    let bone = groupId.x;

    SkinnedState state = gUniforms.skinnedStates.Get(gUniforms.posedInstances[instanceIdx]);
    SkinnedGeom geom = gUniforms.skinnedGeoms.Get(state.geom);

    // The dispatch is sized by the widest rig in the view, so a narrower one stops here rather than
    // reading a bone table that ends before it.
    if (bone >= geom.boneCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    SetMeshOutputCounts(6, 8);

    Mesh mesh = gUniforms.meshes.Get(gUniforms.posedMeshes[instanceIdx]);

    SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);

    float4x4 world = BoneWorld(mesh, state, record, bone);
    float3 head = TranslationOf(world);

    // A bone is drawn from its parent to itself, so the parent is the wide end. A root has none, and
    // a bone standing on its parent has no length either; both fall back to a marker whose axes are
    // the bone's own, which is what keeps a root's orientation readable.
    float3 tail = head;
    float3 axisUp = normalize(world[1].xyz);
    float3 axisSide = normalize(world[0].xyz);

    if (record.parent != cInvalidBone)
    {
        SkinnedBone parentRecord = gUniforms.boneBuffer.Get(geom.bones, record.parent);

        float4x4 parentWorld = BoneWorld(mesh, state, parentRecord, record.parent);
        tail = TranslationOf(parentWorld);
        axisUp = normalize(parentWorld[1].xyz);
        axisSide = normalize(parentWorld[0].xyz);
    }

    float3 along = head - tail;
    float length = sqrt(dot(along, along));

    if (length < c_MinBoneLength)
    {
        // No direction to point: stand the marker up the bone's own axis so it is still a solid.
        along = axisUp * gUniforms.rootSize;
        length = gUniforms.rootSize;
        tail = head - along * 0.5;
    }

    float3 dir = along / length;

    // Roll comes from the parent's basis, not from an arbitrary up: the cross-section then turns with
    // the bone and a twist along its own axis is visible.
    float3 side = axisSide - dir * dot(axisSide, dir);
    if (dot(side, side) < c_MinLengthSq)
    {
        side = axisUp - dir * dot(axisUp, dir);
    }
    side = normalize(side);
    float3 other = cross(dir, side);

    // The waist sits a fifth along, which is the proportion that reads as a bone rather than a
    // spindle. Its width follows the length so a long bone does not become a needle.
    float waist = length * 0.2;
    float half = length * 0.08;

    float3 mid = tail + dir * waist;

    float3 positions[6] = {
        tail,
        mid + side * half,
        mid + other * half,
        mid - side * half,
        mid - other * half,
        head,
    };

    if (gtid < 6)
    {
        VSOut vout;
        vout.position = mul(gUniforms.viewProj, float4(positions[gtid], 1.0));

        // Flat-shaded per triangle would need a per-primitive attribute; the outward direction from
        // the bone's axis is close enough to read the solid's facets and costs nothing.
        float3 fromAxis = positions[gtid] - (tail + dir * dot(positions[gtid] - tail, dir));
        vout.normal = dot(fromAxis, fromAxis) < c_MinLengthSq ? dir : normalize(fromAxis);

        verts[gtid] = vout;
    }

    // Four faces up to the tail, four down to the head, wound so the outside faces out.
    uint3 faces[8] = {
        uint3(0, 2, 1),
        uint3(0, 3, 2),
        uint3(0, 4, 3),
        uint3(0, 1, 4),
        uint3(5, 1, 2),
        uint3(5, 2, 3),
        uint3(5, 3, 4),
        uint3(5, 4, 1),
    };

    if (gtid < 8)
    {
        tris[gtid] = faces[gtid];
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    // Lit off a fixed direction rather than the scene's sun: the overlay has to read the same however
    // the environment is lit, including against a black backdrop.
    const float3 c_KeyDir = normalize(float3(0.4, 0.8, 0.45));

    float3 n = normalize(input.normal);
    float key = saturate(dot(n, c_KeyDir)) * 0.6 + 0.4;

    return float4(gUniforms.color * key, 1.0);
}
