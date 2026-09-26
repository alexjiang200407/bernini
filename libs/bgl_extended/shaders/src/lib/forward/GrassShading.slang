import bgl.LitSurfaceSource;
import bgl.PbrSurface;
import bgl.SurfaceSource;
import idl.GrassLook;
import idl.IPbrMaterial;
import idl.ShadingParams;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.common;
import lib.forward.GrassData;
import lib.forward.grass_vertex;
import lib.geom.vertexdecode;
import lib.math.PbrShading;

// How far past the terminator the sun still shows through a blade, as a share of a right angle.
static const float cGrassTranslucencyWrap = 0.5;

public extension MaterialData
{
    /**
     * The sun through a blade seen against it: a wrapped diffuse term on the far side of `normal`,
     * which the grass stage has turned toward the camera. The one lighting term grass adds, and only
     * to a surface the engine lights -- a lit surface owns all of its lighting.
     */
    public float3 GrassTranslucency(PbrSurface surface, float3 normal, ShadingParams light, float4 translucency)
    {
        let toSun = -light.sunDirection;
        let behind = saturate((dot(-normal, toSun) + cGrassTranslucencyWrap) / (1.0 + cGrassTranslucencyWrap));
        let albedo = surface.baseColor.rgb * (1.0 - surface.orm.b);
        return albedo * translucency.rgb * (translucency.w * behind * surface.orm.r) * light.sunRadiance * light.exposure;
    }

    // A blade's surface lit through the entry every Forward program uses, with the blade's tint and
    // occlusion applied first and its translucency added after.
    float4 ShadeGrassSurface(GrassVSOut input, PbrSurface evaluated)
    {
        var surface = evaluated;
        surface.baseColor.rgb *= input.tint.rgb;
        surface.orm.r *= input.tint.a;

        let lit = ShadeSurface(input.Surface(), surface, true);
        let look = grassData.looks.Get(uint(round(input.uv1.y)));
        let translucent = GrassTranslucency(surface, normalize(input.normal), GetShadingParams(), look.translucency);

        return float4(lit + translucent, 1.0);
    }

    // An engine kind on a blade, as Shade is on a mesh. Its geometry occlusion is read as white: uv1
    // on a blade is not a second UV set.
    public float4 ShadeGrass<M : IPbrMaterial>(GrassVSOut input)
    {
        M material = M.Load(input.materialOffset);

        var unmapped = input.Surface();
        unmapped.uv1 = cNoUv1;
        return ShadeGrassSurface(input, SurfaceOf(material, unmapped, material.GetBaseColor(unmapped.uv)));
    }

    // A registered surface's grass program, as GameOpaqueProgram is its mesh program.
    public ForwardPSOut GameGrassProgram<G : ISurfaceSource>(GrassVSOut input)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        ForwardPSOut output;
        output.color = ShadeGrassSurface(input, record.EvaluateOnGrass(input.Surface()));
        output.motion = ComputeMotionVector(input.Surface());
        return output;
    }

    // A lit surface draws a blade exactly as it shades it: no tint, occlusion or translucency.
    public ForwardPSOut GameLitGrassProgram<G : ILitSurfaceSource>(GrassVSOut input)
    {
        let record = LitGameRecord<G>.Load(input.materialOffset);

        ForwardPSOut output;
        output.color = float4(record.ShadeOnGrass(input.Surface()).rgb * exposure, 1.0);
        output.motion = ComputeMotionVector(input.Surface());
        return output;
    }
}
