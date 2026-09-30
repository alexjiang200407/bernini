import bgl.MaterialReader;
import bgl.SurfaceLight;

/**
 * What a surface that owns its lighting conforms to -- ISurfaceSource's sibling, for a shading
 * model the engine does not define. The parameter struct and Coverage follow the same rules;
 * Shade stands in Evaluate's place and returns radiance, and the engine's own lighting model
 * never runs for it. A file still declares one surface, conforming to one contract or the other.
 *
 * Shade's rgb is radiance in the scene's linear HDR space, before exposure -- the engine applies
 * exposure afterwards, so everything downstream of the forward pass keeps one meaning whatever
 * shaded the pixel. Its a is the surface's coverage, read by the blended layer and ignored on an
 * opaque one.
 */
public interface ILitSurfaceSource
{
    /**
     * Everything a material sets on this surface, under exactly the rules ISurfaceSource states:
     * declared here, reflected at registration, packed by the engine and loaded back for the
     * functions below.
     */
    associatedtype MaterialParams;

    static float Coverage<R : IMaterialReader>(R reader, MaterialParams params);
    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params);
};
