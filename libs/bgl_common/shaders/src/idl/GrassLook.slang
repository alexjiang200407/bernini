// A grass look as the grass stage reads it: bgl::GrassDesc, flattened, with the byte offset of the
// material its blades shade through. One per CreateGrass, rewritten in place by UpdateGrass.
public struct GrassLook
{
    public float4 translucency; // linear colour, and the strength in w
    public float4 rootTint;     // linear colour; w unused
    public float4 tipTint;      // linear colour, and the per-blade variation in w

    public float minHeight;
    public float maxHeight;
    public float rootWidth;
    public float tipWidth;

    public float curvature;
    public float lean;
    public float clumpRadius;
    public float widening;

    public float fadeStart;
    public float fadeEnd;
    public float stiffness;
    public float gustResponse;

    public float rootOcclusion;
    public float normalRounding;
    public float groundNormalNear;
    public float groundNormalFar;

    public uint nearSegments;
    public uint farSegments;
    public uint bladesPerClump;
    public uint materialOffset;
};
