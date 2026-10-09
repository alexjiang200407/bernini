import bgl.LitSurfaceSource;
import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.ToonCharacterSurface;
import idl.ToonShadingRigBlock;
import idl.ToonShadingRigLimits;

// The cel steps over a half-Lambert term already offset by whatever moves the terminator.
float4 ToonCel(ToonCharacterSurface surface, float halfLambert, float3 radiance)
{
    let intoShade = saturate((surface.baseStep - halfLambert) / max(surface.baseFeather, 1e-5));
    let intoSecond = saturate((surface.shadeStep - halfLambert) / max(surface.shadeFeather, 1e-5));

    let shade = lerp(surface.firstShade, surface.secondShade, intoSecond);
    let tone = lerp(float3(1.0), shade, intoShade);
    return float4(surface.baseColor.rgb * tone * radiance, surface.baseColor.a);
}

// What every tone is lit by: the sun, and the environment's irradiance from straight up -- one value
// over the whole character, so a band stays flat where an irradiance along the normal would grade
// it. In the convention PBR's Lambert lobe shares, so a lit tone in full sun reads as a PBR surface
// of the same albedo facing it.
float3 ToonRadiance<L : ISurfaceLight>(L light)
{
    return light.SunRadiance() + light.Irradiance(float3(0.0, 1.0, 0.0));
}

/**
 * The toon character model: three tones over the half-Lambert term, each the base colour times its
 * tint, times the sun's radiance and the sky's ambient (ToonRadiance). Steps of zero keep every
 * pixel lit. Alpha is the base colour's, the coverage the blended layer reads.
 *
 * @param normal World space, unit, facing the viewer.
 */
public float4 ShadeToonCharacter<L : ISurfaceLight>(ToonCharacterSurface surface, L light, float3 normal)
{
    let halfLambert = 0.5 * dot(normal, -light.SunDirection()) + 0.5 + surface.shadeOffset;
    return ToonCel(surface, halfLambert, ToonRadiance(light));
}

// `normal` pulled `amount` of the way toward `target`, both unit; `normal` where the two cancel.
float3 PullNormal(float3 normal, float3 target, float amount)
{
    let pulled = lerp(normal, target, amount);
    return length(pulled) > 1e-6 ? normalize(pulled) : normal;
}

/**
 * One evaluated toon shading rig edit's push on the half-Lambert term at a surface point, the
 * Shading Rig's shape: the direction from the point to the edit, projected azimuthally about the
 * edit's axis and scaled by its size, turned, twisted by its bend and bulge, and fallen off
 * anisotropically; faded to nothing at its radius, and by how far the point faces away from it.
 *
 * @param p The point in head space -- mirrored already, on a mirrored slot.
 * @param n The point's unit normal in head space, mirrored with it.
 */
public float ToonShadingRigSlotOffset(ToonShadingRigSlot slot, float3 p, float3 n)
{
    let d = slot.positionAndGain.xyz - p;
    let dist = length(d);
    if (dist < 1e-6)
    {
        return 0.0;
    }
    let dn = d / dist;

    let u = dot(dn, slot.axisXAndSize.xyz);
    let v = dot(dn, slot.axisYAndAnisotropy.xyz);
    let w = clamp(dot(dn, slot.axisZAndSharpness.xyz), -1.0, 1.0);
    let theta = acos(w);
    let m = theta / (max(length(float2(u, v)), 1e-6) * slot.axisXAndSize.w);
    let x = u * m;
    let y = v * m;

    let cosR = slot.bendBulgeRotation.z;
    let sinR = slot.bendBulgeRotation.w;
    let xr = x * cosR - y * sinR;
    let yr = x * sinR + y * cosR;

    let bend = slot.bendBulgeRotation.x;
    let bulge = slot.bendBulgeRotation.y;
    let t = 10.0 * (bulge * xr + bend * yr);
    let x2 = bulge + cos(t) * (xr - bulge) - sin(t) * (yr - bend);
    let y2 = bend + sin(t) * (xr - bulge) + cos(t) * (yr - bend);
    let front = abs(t) < 1.5707963 ? 1.0 : 0.0;

    let e = max(1.0 - slot.axisYAndAnisotropy.w, 0.02);
    let g = e * x2 * x2 + pow(abs(y2), 2.0 - slot.axisZAndSharpness.w) / e;
    let fall = exp(-g);

    let radius = slot.radiusSmoothingMirror.x;
    let s = saturate((radius - dist) / max(radius * 0.25, 1e-4));
    let smooth = s * s * (3.0 - 2.0 * s);

    let sphere = length(p) > 1e-6 ? normalize(p) : n;
    let ndot = max(dot(PullNormal(n, sphere, slot.radiusSmoothingMirror.y), dn), 0.0);

    return slot.positionAndGain.w * front * smooth * ndot * fall;
}

/**
 * The normal a face's base tone shades with: the surface's, pulled toward the outward normal of the
 * rig's ellipsoid about the head's origin -- the gradient of its implicit form at the point, which
 * goes to world space through the transpose of the head's matrix, as any normal does.
 *
 * @param normal        World space, unit.
 * @param headPos       The point in head space.
 * @param headFromWorld World space to head space, affine.
 * @param faceEllipsoid ToonShadingRigBlock::faceEllipsoid.
 * @param weight        How much of the rig the point takes, in [0, 1].
 * @return World space, unit; `normal` at the head's origin, where the ellipsoid has none.
 */
public float3 ToonFaceNormal(float3 normal, float3 headPos, float4x4 headFromWorld, float4 faceEllipsoid, float weight)
{
    let gradient = headPos * faceEllipsoid.xyz;
    if (length(gradient) < 1e-6)
    {
        return normal;
    }

    let ellipsoid = normalize(mul(float4(gradient, 0.0), headFromWorld).xyz);
    return PullNormal(normal, ellipsoid, faceEllipsoid.w * weight);
}

/**
 * The toon character model on a placement whose toon shading rig was evaluated this draw: a face
 * pixel (`surface.face`) is lit by the rig's face light instead of the sun, shades its base tone
 * with the rig's smoothed normal, and has its terminator moved by every edit, each by as much as it
 * is face times the rig's fade. A pixel that is no face shades exactly as ShadeToonCharacter.
 *
 * @param normal   World space, unit, facing the viewer.
 * @param worldPos The pixel's world position.
 */
public float4 ShadeToonCharacterFace<
    L : ISurfaceLight>(ToonCharacterSurface surface, L light, float3 normal, float3 worldPos, ToonShadingRigBlock rig)
{
    let face = saturate(surface.face);
    if (face <= 0.0)
    {
        return ShadeToonCharacter(surface, light, normal);
    }

    // The rig's fade weighs the face light as it weighs the edits, so a head crossing the fade's
    // end, or leaving the pool, comes back to the sun without a step.
    let weight = face * rig.faceLight.w;
    let toLight = normalize(lerp(-light.SunDirection(), rig.faceLight.xyz, weight));

    let headFromWorld = float4x4(rig.headFromWorld[0], rig.headFromWorld[1], rig.headFromWorld[2], float4(0.0, 0.0, 0.0, 1.0));
    let headPos = mul(headFromWorld, float4(worldPos, 1.0)).xyz;
    let headNormal = normalize(mul(headFromWorld, float4(normal, 0.0)).xyz);

    var offset = 0.0;
    for (uint i = 0; i < min(rig.slotCount, cMaxToonShadingRigSlots); ++i)
    {
        let slot = rig.slots[i];
        let mirror = slot.radiusSmoothingMirror.z > 0.5 ? float3(-1.0, 1.0, 1.0) : float3(1.0, 1.0, 1.0);
        offset += ToonShadingRigSlotOffset(slot, headPos * mirror, headNormal * mirror);
    }
    offset *= weight;

    let baseNormal = ToonFaceNormal(normal, headPos, headFromWorld, rig.faceEllipsoid, weight);
    let halfLambert = 0.5 * dot(baseNormal, toLight) + 0.5 + surface.shadeOffset + offset;
    return ToonCel(surface, halfLambert, ToonRadiance(light));
}

/**
 * A character surface as a lit one, its lighting the engine's toon character model. What a
 * registered character surface is bound as, so it draws through the lit programs -- the record,
 * the reader and every layer -- and the model adds no program family of its own.
 */
public struct ToonCharacterLit<G : IToonCharacterSurfaceSource> : ILitSurfaceSource
{
    public typealias MaterialParams = G.MaterialParams;

    public static float Coverage<R : IMaterialReader>(R reader, MaterialParams params) { return G.Coverage(reader, params); }

    public static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params)
    {
        return ShadeToonCharacter(G.Evaluate(reader, params), light, normalize(reader.WorldNormal()));
    }
};
