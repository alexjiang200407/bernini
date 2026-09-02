// THIS IS A FILE GENERATED FROM IResolveInputs.slang. DO NOT EDIT MANUALLY

// What the temporal resolve reads, as the math reads it: five lookups at a uv, every one at mip
// zero, so the resolve takes no derivative and its driver can be compute. The resolve is generic
// over this so it names no texture; the one implementation today is the pass's constant buffer of
// bindless handles, and a renderer without them supplies its own.
public interface IResolveInputs
{
    /// The accumulated history, filtered: the Catmull-Rom taps land between texels.
    float3 History(float2 uv);
    /// This frame's colour at a texel centre, unfiltered.
    float4 Scene(float2 uv);
    /// The same colour filtered onto the output grid; only a frame with no history reads it.
    float3 SceneLinear(float2 uv);
    /// The velocity buffer at a texel centre, unfiltered.
    float2 Motion(float2 uv);
    /// The depth buffer at a texel centre, unfiltered.
    float Depth(float2 uv);
};

// The plain half of the same inputs, which needs no lookup and so no interface.
public struct ResolveParams
{
    // The unjittered camera this frame and last -- this frame's inverse projection, and its view
    // space into last frame's clip -- for what the camera alone moves a surface by at a pixel's
    // depth. Not the inverse view-projection: docs/taa.md has why. Not valid when more than one
    // draw fed the frame.
    public float4x4 clipToView;
    public float4x4 viewToPrevClip;
    public float cameraPairValid;

    // This frame's sub-pixel offset in NDC: a pixel's depth is the fragment's at the jittered
    // sample, so the surface behind it sits at the centre less the jitter.
    public float2 jitter;

    // The two grids. The scene colour, motion and depth are on the render one; the resolve, the
    // history and the backbuffer are on the output one. Equal at render scale 1.0.
    public float2 renderSize;
    public float2 renderTexelSize;
    public float2 outputTexelSize;

    // This frame's offset from a render pixel's centre, in render texels -- where the current
    // frame's sample actually landed, which is what decides how much of it each output pixel takes.
    public float2 jitterTexels;

    // Output pixels per render pixel along each axis, at least one. The phases PhaseWeight
    // normalizes over.
    public float2 subPixels;

    // Whether the two grids differ at all. Only the no-history path reads it: everything else is
    // written so that one sample per output pixel falls out as the untouched render-grid resolve.
    public float resampling;

    // Reciprocal of twice the squared width of PhaseWeight's kernel, in output pixels. A knob rather
    // than a constant because narrowing it sharpens a held frame without limit while lengthening the
    // wait a moving pixel has for the phase that serves it, and that trade is judged by eye at a
    // render scale. It cannot move an image at scale 1.0: one phase weighs itself against its own
    // mean whatever the width is.
    public float sampleWeightK;

    // How much of the resolved pixel comes from this frame. Zero history -- the first frame, or the
    // first after a resize -- takes the scene colour whole.
    public float blendWeight;
    public float historyValid;
};
