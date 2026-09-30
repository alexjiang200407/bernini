import idl.Clip;
import idl.Constants;
import idl.IBonePalette;
import idl.Rig;
import lib.anim.clip_playback;
import lib.anim.pose_walk;

// The vertex half of skinning: which two frames of a rig's table a fractional frame spans, and the
// blend of a vertex's influencing bones out of whatever palette holds them. The pose half --
// producing those palettes -- is pose_walk.

public void TableFrameSlots(Rig rig, Clip clip, float frames, out uint lower, out uint upper, out float weight)
{
    uint f0;
    uint f1;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    let stride = rig.boneCount * cFloat4sPerBone;
    lower = rig.boneAnimTable.GetStart() + (clip.firstFrame + f0) * stride;
    upper = rig.boneAnimTable.GetStart() + (clip.firstFrame + f1) * stride;
}

// Linear blend skinning: the weighted sum of the influencing bones' matrices, applied once rather
// than per attribute, so position, normal and tangent all follow the same blend.
//
// No renormalising divide: the importer already normalises a vertex whose weights sum to anything
// other than zero (assetlib's readSkinAttributes), so paying for it per vertex here would only paper
// over a bad cook. A vertex bound to no bone never reaches here -- the caller answers that one
// ahead of the blend.
public float4x4 SkinMatrix<P : IBonePalette>(P palette, uint slotBase, float4 joints, float4 weights)
{
    float4x4 skin = weights.x * PaletteMatrix(palette, slotBase + uint(joints.x) * cFloat4sPerBone);

    [unroll]
    for (uint i = 1; i < cInfluencesPerVertex; ++i)
    {
        // A zero weight still costs the fetch; branching on it diverges the wave for no saving, since
        // the influences of one vertex are almost always all live or all dead together.
        skin += weights[i] * PaletteMatrix(palette, slotBase + uint(joints[i]) * cFloat4sPerBone);
    }

    return skin;
}

// A fractional frame is the blend of the two frames' *skin matrices*, not of two skinned positions:
// skinning is linear in the matrices, so the two agree, and this pays one blend per vertex instead
// of one per attribute. `blends` is group-uniform, so a palette never pays the second fetch and the
// branch costs no divergence.
public float4x4 SkinAt<P : IBonePalette>(P palette, bool blends, uint slotBase, uint upperBase, float weight, float4 joints, float4 weights)
{
    let lower = SkinMatrix(palette, slotBase, joints, weights);

    if (!blends)
    {
        return lower;
    }

    return lerp(lower, SkinMatrix(palette, upperBase, joints, weights), weight);
}
