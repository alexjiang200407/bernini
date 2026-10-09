import bgl.LitSurfaceSource;
import bgl.SurfaceSource;
import idl.IPbrMaterial;
import lib.forward.common;
import lib.forward.GameSurface;
import lib.forward.MaterialData;

// A terrain's ground as the Ground Color pass writes it: the surface's base colour, unlit, in the
// colour target's rgb and in its alpha whether there is one, and in the cover target how much of
// the ground the surface covers (PbrSurface::groundCover). Terrain grass reads the colour under
// each blade's root and the cover under each clump (docs/grass.md § Ground colour, § Ground cover).

public struct GroundColorOut
{
    public float4 color : SV_Target0;
    public float cover : SV_Target1;
};

public extension MaterialData
{
    // An engine kind's base colour where the terrain patch stands, covering all of it.
    public GroundColorOut GroundColorOf<M : IPbrMaterial>(ForwardVSOut input)
    {
        M material = M.Load(input.materialOffset);
        GroundColorOut ground;
        ground.color = float4(material.GetBaseColor(input.uv).rgb, 1.0);
        ground.cover = 1.0;
        return ground;
    }

    // A registered surface's, evaluated with the vertex the top-down draw emits and the view's own
    // camera: an albedo that reads the view direction or the screen position is not the one the
    // colour pass shades.
    public GroundColorOut GameGroundColorProgram<G : ISurfaceSource>(ForwardVSOut input)
    {
        let record = GameRecord<G>.Load(input.materialOffset);
        let surface = record.Evaluate(input, true);
        GroundColorOut ground;
        ground.color = float4(surface.baseColor.rgb, 1.0);
        ground.cover = saturate(surface.groundCover);
        return ground;
    }

    // A lit surface owns its lighting and has no albedo apart from it: no ground colour, so the
    // blades over it keep their own, and nothing said about the cover, so all of it.
    public GroundColorOut GameLitGroundColorProgram<G : ILitSurfaceSource>(ForwardVSOut input)
    {
        GroundColorOut ground;
        ground.color = float4(0.0, 0.0, 0.0, 0.0);
        ground.cover = 1.0;
        return ground;
    }
}
