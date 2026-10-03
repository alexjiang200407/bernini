// Where each leg of a posed rig stands: its heel and its ball, written beside the pose for the
// blob-shadow pass's foot shadows. Both producers of a pose call it -- PoseSkinned into a hero's
// palette, in world space and after the plant; PoseRigFrames into a rig's table soles, in model
// space and unplanted -- so the two cannot measure a foot differently.

import idl.Constants;
import idl.IPoseTables;
import idl.Rig;
import idl.SkinnedLegChain;
import lib.anim.foot_plant;
import lib.anim.pose_walk;
import lib.math.transform;
import lib.types.RangeBuffer;

// Writes each leg's sole from `firstSoleSlot`, cFloat4sPerSole a leg, read off the model transforms
// the walk left in the palette from `paletteBase` -- so it runs between PoseModelSpace and
// FoldInverseBind, the one window those slots hold one. `modelToWorld` places them; identity keeps
// them in model space.
//
// @pre group-uniform arguments, since it ends on a group barrier.
public void WriteSoles<T : IPoseTables>(
    T tables,
    RangeBuffer<SkinnedLegChain> legBuffer,
    Rig rig,
    float4x4 modelToWorld,
    uint paletteBase,
    uint firstSoleSlot,
    uint localIdx
)
{
    if (localIdx < rig.legs.count)
    {
        let leg = legBuffer.Get(rig.legs, localIdx);
        let ankleModel = PaletteMatrix(tables, paletteBase + leg.ankle * cFloat4sPerBone);
        let toeModel = PaletteMatrix(tables, paletteBase + leg.toe * cFloat4sPerBone);
        let ankle = float3(ankleModel[0].w, ankleModel[1].w, ankleModel[2].w);

        float3 heel;
        float3 ball;
        HeelAndBall(ankleModel, float3(toeModel[0].w, toeModel[1].w, toeModel[2].w), leg.solePoint.xyz, leg.soleNormal.xyz, heel, ball);

        let slot = firstSoleSlot + localIdx * cFloat4sPerSole;
        tables.SetPalette(slot + 0, float4(TransformPoint(modelToWorld, ankle + heel), 1.0));
        tables.SetPalette(slot + 1, float4(TransformPoint(modelToWorld, ankle + ball), 1.0));
    }

    // The fold rewrites the slots read above from whichever thread owns them.
    AllMemoryBarrierWithGroupSync();
}
