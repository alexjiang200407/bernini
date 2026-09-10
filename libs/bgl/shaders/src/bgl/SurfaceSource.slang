import bgl.MaterialReader;
import bgl.PbrSurface;

/**
 * What a surface written outside the engine conforms to: a parameter struct the engine packs and
 * loads, and two functions over a reader. Coverage runs first on a tested layer and discards before
 * Evaluate, so a cheap coverage answers without the rest of the surface's samples.
 */
public interface ISurfaceSource
{
    /**
     * Everything a material sets on this surface: its numbers, and its textures as slots. A surface
     * declares the struct and never fills one -- the engine reflects it at registration to learn
     * where each field sits, packs a material's values into a record at those offsets, and loads
     * the struct back out of the record to hand to the functions below. So the layout comes from
     * the code that reads it, and there is no second file to keep in step.
     */
    associatedtype MaterialParams;

    static float Coverage<R : IMaterialReader>(R reader, MaterialParams params);
    static PbrSurface Evaluate<R : IMaterialReader>(R reader, MaterialParams params);
};
