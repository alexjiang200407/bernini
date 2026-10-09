import bgl.LitSurfaceSource;
import bgl.SurfaceSource;
import idl.IPbrMaterial;
import lib.forward.common;
import lib.forward.GameSurface;
import lib.forward.MaterialData;

// A terrain's albedo as the Ground Color pass writes it: the surface's base colour, unlit, in rgb,
// and in alpha whether there is one. Terrain grass reads it under each blade's root
// (docs/grass.md § Ground colour).

public extension MaterialData
{
    // An engine kind's base colour where the terrain patch stands.
    public float4 GroundColorOf<M : IPbrMaterial>(ForwardVSOut input)
    {
        M material = M.Load(input.materialOffset);
        return float4(material.GetBaseColor(input.uv).rgb, 1.0);
    }

    // A registered surface's, evaluated as it is when the terrain draws, from above.
    public float4 GameGroundColorProgram<G : ISurfaceSource>(ForwardVSOut input)
    {
        let record = GameRecord<G>.Load(input.materialOffset);
        return float4(record.Evaluate(input, true).baseColor.rgb, 1.0);
    }

    // A lit surface owns its lighting and has no albedo apart from it: no ground colour, so the
    // blades over it keep their own.
    public float4 GameLitGroundColorProgram<G : ILitSurfaceSource>(ForwardVSOut input)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
}
