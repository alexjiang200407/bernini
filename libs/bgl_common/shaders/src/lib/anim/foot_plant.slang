// The geometry a planted foot is solved with: where the ground is under a point, where a foot's
// contact sits on its sole plane, and the two-bone solve that reaches for it.
//
// Values only -- the caller fetches the ground out of its uniforms and the joints out of its
// palette, and passes them in. What a leg owns is where the target comes from and what weights it;
// none of that is here.
import lib.math.transform;

// The ground plane carried into an instance's model space, as `(normal, -distance)` with a unit
// normal.
//
// Carried as a plane rather than sampled in world space: a plane is a row vector, so it crosses a
// transform by multiplication from the left and needs no inverse of its own. The inverse is still
// wanted for the drop direction, which is why a caller builds both once per group.
public float4 GroundPlaneInModel(float3 worldNormal, float3 worldPoint, float4x4 modelToWorld)
{
    float4 world = float4(worldNormal, -dot(worldNormal, worldPoint));
    float4 model = mul(world, modelToWorld);
    return model / length(model.xyz);
}

// Where the ground sits under `point`, dropped along world down rather than along the plane's own
// normal: a planted foot must keep the ground position it was authored over, and the closest point
// on a slope is not that point.
//
// `down` is world -Y carried into model space. SetGround refuses a normal with no upward component,
// so the denominator here cannot be zero.
public float3 SampleGround(float4 plane, float3 down, float3 point)
{
    let distance = dot(plane.xyz, point) + plane.w;
    return point + down * (distance / -dot(plane.xyz, down));
}

// How far the ground has moved away from the floor the clip was authored on, under `point`.
//
// This is what a plant applies, rather than the ground's absolute position. `groundClips` rests
// every clip's lowest point on model `y = 0` -- measured, or the `.bimport`'s `clipFloor` for a clip
// whose lowest frame is not its standing one -- so that plane is the floor the animator posed the
// foot against, whatever the clip. Taking the departure from it makes a plant on flat ground at the
// authored height an exact identity, where placing the contact on the ground absolutely corrects by
// however far the contact sits above the clip's lowest point: the sole plane is fitted a
// centimetre-scale band up from the lowest vertex, so that residual stretches every leg slightly on
// ground the clip was already standing on. The standard solve is framed the same way -- it keeps a
// foot's animated height relative to the root and adds the ground beneath it.
// Two terms. The first puts the turned contact on the ground, which is what keeps the foot's lowest
// point out of the floor on a slope. The second gives back the height the clip's own contact stood
// above its own floor -- `authoredContact` is that contact with no turn applied -- which is the part
// a plant must not discard: the sole plane is fitted a band up from the lowest vertex `groundClips`
// rested on zero, so a foot standing in its clip already hovers a centimetre or so, and pinning it
// flat is what stretches every leg on ground the clip was already standing on.
//
// On flat ground at the authored height the two cancel exactly and the plant is an identity.
//
// `authored` is that floor as a plane, which a caller that has lowered the rig must lower with it:
// the floor is rigid with the pose, and a departure measured against a floor the rig has moved off
// is one the rig then chases. Lowering by `d` along the ground's up `n` makes it
// `float4(0, 1, 0, d * n.y)`. With that, the departure rises by `d * n` exactly as the joints fall,
// so the target is invariant under the drop -- which is what lets a caller re-read a leg after
// lowering and get the target it measured before.
public float3 GroundDeparture(float4 plane, float4 authored, float3 down, float3 contact, float3 authoredContact)
{
    return (SampleGround(plane, down, contact) - contact) + (authoredContact - SampleGround(authored, down, authoredContact));
}

// A point projected onto the sole plane, the plane through `sole` with normal `soleUp`. All three
// are ankle-relative.
public float3 OnSolePlane(float3 point, float3 sole, float3 soleUp)
{
    return point - dot(point - sole, soleUp) * soleUp;
}

// The analytic two-bone solve: law of cosines on a root-mid-tip chain toward `target`, keeping the
// bend in the plane the chain is already bent in, then one aim rotation bringing the tip onto the
// target.
//
// Limb-neutral on purpose. A leg is what calls it today, but nothing below knows that: a shoulder,
// elbow and wrist reaching for a prop is the same three joints and the same maths, so whoever adds
// that reaches for this rather than writing a second copy of it.
//
// `rootDelta` and `midDelta` come back as the model-space transforms to premultiply onto the root
// and onto everything from the mid joint down -- the tip included, since it is rigid below the mid.
public void SolveTwoBone(float3 root, float3 mid, float3 tip, float3 target, out float4x4 rootDelta, out float4x4 midDelta)
{
    let upper = length(mid - root);
    let lower = length(tip - mid);

    // Held just inside full extension: at exactly upper + lower the bend plane is undefined, and one
    // frame of a straight chain reads as a snap.
    let reach = clamp(length(target - root), 1e-4, upper + lower - 1e-4);

    let toTip = normalize(tip - root);
    let toMid = normalize(mid - root);

    // The plane the chain is bent in. One that is already straight has no such plane, so nothing can
    // be said about which way it should bend and the solve leaves it alone.
    float3 axis = cross(toTip, toMid);
    if (length(axis) < 1e-6)
    {
        rootDelta = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
        midDelta = rootDelta;
        return;
    }
    axis = normalize(axis);

    let rootNow = acos(clamp(dot(toTip, toMid), -1.0, 1.0));
    let midNow = acos(clamp(dot(normalize(root - mid), normalize(tip - mid)), -1.0, 1.0));

    let rootWanted = acos(clamp((upper * upper + reach * reach - lower * lower) / (2.0 * upper * reach), -1.0, 1.0));
    let midWanted = acos(clamp((upper * upper + lower * lower - reach * reach) / (2.0 * upper * lower), -1.0, 1.0));

    let bendRoot = RotationAbout(root, axis, rootWanted - rootNow);
    let mid1 = TransformPoint(bendRoot, mid);
    let bendMid = RotationAbout(mid1, axis, midWanted - midNow);

    let tip1 = TransformPoint(bendMid, TransformPoint(bendRoot, tip));

    // Now the chain is the right shape but pointing where it was; swing it onto the target.
    let from = normalize(tip1 - root);
    let to = normalize(target - root);
    float3 aimAxis = cross(from, to);

    float4x4 aim = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
    if (length(aimAxis) > 1e-6)
    {
        aim = RotationAbout(root, normalize(aimAxis), acos(clamp(dot(from, to), -1.0, 1.0)));
    }

    rootDelta = mul(aim, bendRoot);
    midDelta = mul(aim, mul(bendMid, bendRoot));
}
