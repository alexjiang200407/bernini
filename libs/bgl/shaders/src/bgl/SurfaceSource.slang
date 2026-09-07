import bgl.MaterialReader;
import bgl.PbrSurface;

/**
 * What a surface written outside the engine conforms to: a parameter struct the engine packs and
 * loads, and two functions over a reader. Coverage runs first on a tested layer and discards before
 * Evaluate, so a cheap coverage answers without the rest of the surface's samples.
 */
public interface ISurfaceSource
{
    associatedtype Params;

    static float Coverage<R : IMaterialReader>(R reader, Params params);
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, Params params);
};
