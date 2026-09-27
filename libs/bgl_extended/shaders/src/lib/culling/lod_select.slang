// Choosing a placement's level of detail: its size on screen, the level that size earns, the
// hysteresis that keeps it from switching on a threshold, and the dissolve a change starts. One
// placement, one choice -- every submesh-instance thread of a placement calls these with the same
// inputs and lands on the same word, which is what keeps a mesh from cracking at a seam.
//
// A level index equal to the geom's level count is the draw-nothing tier: the placement is smaller
// than its last level's threshold.

import idl.Constants;
import idl.CullView;
import idl.Geom;
import idl.InstanceLod;
import idl.LodLevel;
import idl.MeshInstance;

// What a placement spans when the camera stands inside its bound: larger than any threshold.
static const float cInsidePixels = 3.4e38;

// The placement's diameter on screen, in pixels, at its true Euclidean distance -- not view-space
// depth, which would change a level as the camera turns.
public float ProjectedDiameter(MeshInstance mesh, Geom geom, CullView view)
{
    let sphere = mesh.TransformSphere(geom.boundingSphere);
    let distance = length(sphere.xyz - view.cameraPosAndPixelsPerUnit.xyz);
    if (distance <= sphere.w)
    {
        return cInsidePixels;
    }
    return 2.0 * sphere.w * view.cameraPosAndPixelsPerUnit.w / distance;
}

// The finest level whose floor, scaled, `size` meets, or the level count when it meets none.
public uint LevelBySize(Geom geom, float size, float floorScale)
{
    for (uint level = 0u; level < geom.submeshes.lodCount; ++level)
    {
        if (size >= geom.lodMinPixels[level] * floorScale)
        {
            return level;
        }
    }
    return geom.submeshes.lodCount;
}

// The level to draw this frame. Coarser happens at the threshold; finer only once the size clears
// that level's floor by cLodHysteresis, so a placement resting on a threshold stays where it is.
public uint ChooseLevel(Geom geom, CullView view, float size, InstanceLod previous)
{
    if (view.lodForcedLevel != cLodForceNone)
    {
        return min(view.lodForcedLevel, geom.submeshes.lodCount - 1u);
    }

    let atThreshold = LevelBySize(geom, size, view.lodPixelScale);
    if (previous.Unset())
    {
        return atThreshold;
    }

    let current = uint(previous.Level());
    if (atThreshold >= current)
    {
        return atThreshold;
    }
    return min(LevelBySize(geom, size, view.lodPixelScale * (1.0 + cLodHysteresis)), current);
}

// This frame's word. A change of level starts a dissolve out of the level drawn until now; a
// dissolve in progress advances by the view's step and ends at 1. `snap` makes the change at once:
// a placement no one sees, or a forced level, has nothing to hide.
public InstanceLod AdvanceLod(InstanceLod previous, uint chosen, float step, bool snap)
{
    let level = LodLevel(chosen);
    if (previous.Unset() || snap)
    {
        return InstanceLod.Make(level, false, level, 1.0);
    }

    let current = uint(previous.Level());
    if (chosen != current)
    {
        return step >= 1.0 ? InstanceLod.Make(level, false, level, 1.0) : InstanceLod.Make(level, true, LodLevel(current), step);
    }
    if (previous.Fading())
    {
        // Within one stored step of the end is the end: the word keeps the fade in 16 bits, and
        // steps that sum to 1 would otherwise leave it a quantum short, a frame too long.
        let fade = previous.Fade() + step;
        return fade >= 1.0 - 1.0 / cInstanceLodFadeScale ? InstanceLod.Make(level, false, level, 1.0)
                                                         : InstanceLod.Make(level, true, previous.OutgoingLevel(), fade);
    }
    return previous;
}
