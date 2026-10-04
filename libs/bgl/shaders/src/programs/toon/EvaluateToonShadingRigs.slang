import idl.Clip;
import idl.Constants;
import idl.DominantFrames;
import idl.InstancePose;
import idl.MeshInstance;
import idl.MeshInstanceFlag;
import idl.PlaybackType;
import idl.Rig;
import idl.SkinnedAutoState;
import idl.SkinnedBone;
import idl.SkinnedState;
import idl.SkinnedTableState;
import idl.ToonShadingRig;
import idl.ToonShadingRigBlock;
import idl.ToonShadingRigLimits;
import idl.ToonShadingRigPool;
import idl.ToonShadingRigRange;
import lib.anim.clip_playback;
import lib.anim.skinning;
import lib.math.frustum;
import lib.math.transform;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One thread per placement holding a toon shading rig: selects it -- not hidden, its head in the
// frustum and larger on screen than the rig's fade end -- into the view's pool, evaluates the rig
// against the sun in its head space into the block it took, and tells its draws which block that is
// through the upper bits of its flags word. Every rigged placement's bits are rewritten each draw,
// so one that drops out of the pool shades cel only on the same frame.

struct Uniforms
{
    UploadBuffer<ToonShadingRigRange> ranges;
    uint rangeCount;
    uint threadCount;

    // Writable for the flags word's slot bits alone. ScalarDataLayout as lib.types.EntryBuffer.
    RWStructuredBuffer<MeshInstance, ScalarDataLayout>.Handle meshes;

    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;
    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    ComputeBuffer<float4> bonePalettes;
    ComputeBuffer<float4> boneAnimTables;
    ComputeBuffer<InstancePose> instancePose;
    ComputeBuffer<DominantFrames> dominantFrames;

    EntryBuffer<ToonShadingRig> toonRigs;
    RangeBuffer<ToonShadingRigKey> toonKeys;

    ComputeBuffer<ToonShadingRigPool> pool;
    ComputeBuffer<ToonShadingRigBlock> blocks;

    float4 frustumPlanes[6];
    float3 cameraPos;
    float pixelsPerUnit;

    // Toward the sun, world space, unit.
    float3 toLight;
    float time;
};

ConstantBuffer<Uniforms> gUniforms;

float4x4 Rows(float4 rows[3])
{
    return float4x4(rows[0], rows[1], rows[2], float4(0.0, 0.0, 0.0, 1.0));
}

float4x4 PaletteAt(ComputeBuffer<float4> source, uint slot)
{
    return float4x4(source[slot], source[slot + 1u], source[slot + 2u], float4(0.0, 0.0, 0.0, 1.0));
}

// The skinning matrix of `bone` at this frame's clock, from whichever source the placement drew:
// its palette, its pose-pool slice, or its rig's table between two frames -- as ResolveSkinnedPose.
float4x4 SkinOf(MeshInstance mesh, uint placement, uint bone, out Rig rig)
{
    let kind = PlaybackType(gUniforms.playbackBuffer.LoadTag(mesh.playback));
    if (kind == PlaybackType.kSkinnedAuto)
    {
        let state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedAutoState>(mesh.playback);
        rig = gUniforms.rigs.Get(state.rig);

        let slice = gUniforms.instancePose[placement].palette;
        if (!slice.Null())
        {
            return PaletteAt(gUniforms.bonePalettes, slice.GetStart() + bone * cFloat4sPerBone);
        }

        let dominant = gUniforms.dominantFrames[placement];
        let start = rig.boneAnimTable.GetStart();
        let stride = rig.boneCount * cFloat4sPerBone;
        let lower = PaletteAt(gUniforms.boneAnimTables, start + dominant.lowerFrame * stride + bone * cFloat4sPerBone);
        let upper = PaletteAt(gUniforms.boneAnimTables, start + dominant.upperFrame * stride + bone * cFloat4sPerBone);
        return lerp(lower, upper, dominant.upperWeight);
    }

    if (kind == PlaybackType.kSkinnedTable)
    {
        let state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedTableState>(mesh.playback);
        rig = gUniforms.rigs.Get(state.playback.rig);
        let clip = gUniforms.clipBuffer.Get(rig.clips, state.playback.clip);

        uint lower;
        uint upper;
        float weight;
        TableFrameSlots(rig, clip, ClipFrames(clip, state.playback, gUniforms.time), lower, upper, weight);
        let offset = bone * cFloat4sPerBone;
        return lerp(PaletteAt(gUniforms.boneAnimTables, lower + offset), PaletteAt(gUniforms.boneAnimTables, upper + offset), weight);
    }

    let state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedState>(mesh.playback);
    rig = gUniforms.rigs.Get(state.rig);
    return PaletteAt(gUniforms.bonePalettes, state.palette.GetStart() + bone * cFloat4sPerBone);
}

// The head's frame in the world: the placement's transform, the head bone's model transform if the
// rig names one -- its stored skin matrix with the inverse bind taken back out -- and the rig's own.
float4x4 HeadWorld(MeshInstance mesh, uint placement, ToonShadingRig toon)
{
    var model = IdentityTransform();
    if (toon.headBoneIndex != cNoHeadBone && !mesh.playback.Null())
    {
        Rig rig;
        let skin = SkinOf(mesh, placement, toon.headBoneIndex, rig);
        let bone = gUniforms.boneBuffer.Get(rig.bones, toon.headBoneIndex);
        model = mul(skin, AffineInverse(bone.inverseBind));
    }
    return mul(mesh.Matrix(), mul(model, Rows(toon.headToBone)));
}

// The face's light: the sun in head space, its elevation clamped, its azimuth swung toward the
// front as it rises and clamped. Head space is +X across the face, +Y up, +Z out of it.
float3 RemapFaceLight(ToonShadingRig toon, float3 eta)
{
    var azimuth = atan2(eta.x, eta.z);
    var elevation = asin(clamp(eta.y, -1.0, 1.0));

    var t = saturate((elevation - toon.azimuthFadeStart) / (toon.azimuthFadeEnd - toon.azimuthFadeStart));
    t = t * t * (3.0 - 2.0 * t);
    azimuth *= 1.0 - t * toon.azimuthFadeAmount;
    azimuth = clamp(azimuth, -toon.maxAzimuth, toon.maxAzimuth);
    elevation = clamp(elevation, toon.minElevation, toon.maxElevation);

    return float3(sin(azimuth) * cos(elevation), sin(elevation), cos(azimuth) * cos(elevation));
}

// One edit's keys blended for `light`: a normalized spherical-Gaussian weight per key, so the blend
// never leaves the hull of the keys, then the edit's sign lock.
ToonShadingRigSlot EvaluateEdit(ToonShadingRig toon, ToonShadingRigEdit edit, float3 light, bool mirror)
{
    var weightSum = 0.0;
    var lightAndGain = float4(0.0);
    var positionAndSize = float4(0.0);
    var shape = float4(0.0);
    var rotationRadiusSmoothing = float4(0.0);

    for (uint k = 0; k < edit.keyCount; ++k)
    {
        let key = gUniforms.toonKeys.Get(toon.keys, edit.firstKey + k);
        let w = exp(edit.keySharpness * (dot(key.lightAndGain.xyz, light) - 1.0));
        weightSum += w;
        lightAndGain += w * key.lightAndGain;
        positionAndSize += w * key.positionAndSize;
        shape += w * key.shape;
        rotationRadiusSmoothing += w * key.rotationRadiusSmoothing;
    }

    let norm = 1.0 / max(weightSum, 1e-12);
    var gain = lightAndGain.w * norm;
    let position = positionAndSize.xyz * norm;
    let size = positionAndSize.w * norm;
    shape *= norm;
    rotationRadiusSmoothing *= norm;

    if (edit.Has(ToonShadingRigEditFlag.kShadeOnly))
    {
        gain = min(gain, 0.0);
    }
    else if (edit.Has(ToonShadingRigEditFlag.kLightOnly))
    {
        gain = max(gain, 0.0);
    }

    let lz = length(position) > 1e-6 ? normalize(position) : float3(0.0, 0.0, 1.0);
    let across = cross(float3(0.0, 1.0, 0.0), lz);
    let lx = length(across) > 1e-6 ? normalize(across) : float3(1.0, 0.0, 0.0);
    let ly = cross(lz, lx);
    let rotation = rotationRadiusSmoothing.x;

    ToonShadingRigSlot slot;
    slot.positionAndGain = float4(position, gain);
    slot.axisXAndSize = float4(lx, max(size, 1e-4));
    slot.axisYAndAnisotropy = float4(ly, clamp(shape.x, 0.0, 0.98));
    slot.axisZAndSharpness = float4(lz, saturate(shape.y));
    slot.bendBulgeRotation = float4(shape.z, shape.w, cos(rotation), sin(rotation));
    slot.radiusSmoothingMirror = float4(max(rotationRadiusSmoothing.y, 0.0), saturate(rotationRadiusSmoothing.z), mirror ? 1.0 : 0.0, 0.0);
    return slot;
}

// The range thread `thread` falls in: the last whose first thread is at or before it.
uint FindRange(uint thread)
{
    var lo = 0u;
    var hi = gUniforms.rangeCount - 1u;
    while (lo < hi)
    {
        let mid = (lo + hi + 1u) / 2u;
        if (gUniforms.ranges[mid].firstThread <= thread)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1u;
        }
    }
    return lo;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint thread: SV_DispatchThreadID)
{
    if (thread >= gUniforms.threadCount)
    {
        return;
    }

    let range = gUniforms.ranges[FindRange(thread)];
    let placement = range.firstPlacement + (thread - range.firstThread);
    let mesh = gUniforms.meshes[placement];
    let unselected = mesh.flags & ~(cToonShadingRigSlotMask << cToonShadingRigSlotShift);

    if (HasMeshInstanceFlag(mesh, MeshInstanceFlag.kHidden))
    {
        gUniforms.meshes[placement].flags = unselected;
        return;
    }

    let toon = gUniforms.toonRigs.Get(range.rig);
    let headWorld = HeadWorld(mesh, placement, toon);
    let origin = float3(headWorld[0].w, headWorld[1].w, headWorld[2].w);

    let scale = length(mesh.TransformDirection(float3(1.0, 0.0, 0.0)));
    let radius = toon.headRadius * scale;
    let distance = length(origin - gUniforms.cameraPos);
    let pixels = distance > radius ? 2.0 * radius * gUniforms.pixelsPerUnit / distance : 1e30;

    if (!SphereInFrustum(gUniforms.frustumPlanes, origin, radius) || pixels <= toon.fadeEndPixels)
    {
        gUniforms.meshes[placement].flags = unselected;
        return;
    }

    let slot = gUniforms.pool[0].selected.add(1u);
    if (slot >= cToonShadingRigPoolCapacity)
    {
        gUniforms.meshes[placement].flags = unselected;
        return;
    }

    // The head's rotation, its scale divided out of each axis.
    let axisX = normalize(float3(headWorld[0].x, headWorld[1].x, headWorld[2].x));
    let axisY = normalize(float3(headWorld[0].y, headWorld[1].y, headWorld[2].y));
    let axisZ = normalize(float3(headWorld[0].z, headWorld[1].z, headWorld[2].z));

    let toLight = gUniforms.toLight;
    let eta = normalize(float3(dot(axisX, toLight), dot(axisY, toLight), dot(axisZ, toLight)));
    let face = RemapFaceLight(toon, eta);

    ToonShadingRigBlock block;
    let headFromWorld = AffineInverse(headWorld);
    block.headFromWorld[0] = headFromWorld[0];
    block.headFromWorld[1] = headFromWorld[1];
    block.headFromWorld[2] = headFromWorld[2];
    block.faceLight = float4(
        normalize(axisX * face.x + axisY * face.y + axisZ * face.z),
        saturate((pixels - toon.fadeEndPixels) / (toon.fadeStartPixels - toon.fadeEndPixels))
    );
    block.placement = placement;
    block.pad[0] = 0u;
    block.pad[1] = 0u;

    var written = 0u;
    for (uint e = 0; e < toon.editCount; ++e)
    {
        let edit = toon.edits[e];
        block.slots[written++] = EvaluateEdit(toon, edit, face, false);
        if (edit.Has(ToonShadingRigEditFlag.kMirrored))
        {
            block.slots[written++] = EvaluateEdit(toon, edit, float3(-face.x, face.y, face.z), true);
        }
    }
    block.slotCount = written;

    gUniforms.blocks[slot] = block;
    gUniforms.meshes[placement].flags = unselected | ((slot + 1u) << cToonShadingRigSlotShift);
}
