import bgl.MaterialReader;
import bgl.SurfaceLight;

/**
 * What the engine hands a water surface: everything IMaterialReader gives, and the three things of
 * the frame water is drawn from. The interpolants are the water mesh's, so WorldPos is the point on
 * the water's surface the pixel shows.
 */
public interface IWaterMaterialReader : IMaterialReader
{
    /// The metres from the water's surface to the opaque scene behind it, along the view ray. Large
    /// where nothing opaque is behind the water: the sky.
    float ViewDepth();

    /// The metres from the water's surface straight down to the terrain under it, read from the
    /// scene's heightfield rather than the depth buffer, so a band of it keeps one width in the
    /// world at any viewing angle. Negative where the ground stands above the water, and large
    /// where no terrain lies under the pixel.
    float GroundDepth();

    /// The view's clock in seconds, for scrolling.
    float Time();
};

/**
 * What a water surface conforms to: ILitSurfaceSource's shape over the water reader. The parameter
 * struct follows ISurfaceSource's rules; there is no Coverage, because water has no alpha layer and
 * is always blended over what is behind it.
 *
 * Shade's rgb is radiance before exposure, as a lit surface's; its a is how much of what is behind
 * the water it hides. The engine weights the rgb by the a and composites it over the scene, as it
 * does a lit surface's blended layer.
 */
public interface IWaterSurfaceSource
{
    /// Everything a material sets on this surface, under the rules ISurfaceSource states.
    associatedtype MaterialParams;

    static float4 Shade<R : IWaterMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params);
};
