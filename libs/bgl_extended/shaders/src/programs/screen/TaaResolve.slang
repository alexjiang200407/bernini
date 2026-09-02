import lib.math.taa;
struct TaaResolveData
{
    Texture2D.Handle sceneColor;
    Texture2D.Handle history;
    Texture2D.Handle motionVectors;
    Texture2D.Handle depth;

    // The unjittered camera this frame and last -- this frame's inverse projection, and its view
    // space into last frame's clip -- for what the camera alone moves a surface by at a pixel's
    // depth. Not the inverse view-projection: docs/taa.md has why. Not valid when more than one
    // draw fed the frame.
    float4x4 clipToView;
    float4x4 viewToPrevClip;
    float cameraPairValid;

    // This frame's sub-pixel offset in NDC: a pixel's depth is the fragment's at the jittered
    // sample, so the surface behind it sits at the centre less the jitter.
    float2 jitter;

    // sceneColor and motionVectors are read at their own texel centres and want no filtering; the
    // history is reprojected to an arbitrary sub-pixel position and goes through
    // SampleHistoryCatmullRom's bilinear taps. The linear sampler also carries the scene colour to
    // the output grid on a frame with no history to reproject.
    SamplerState.Handle pointSampler;
    SamplerState.Handle linearSampler;

    // The two grids. sceneColor, motionVectors and depth are on the render one; this pass, the
    // history and the backbuffer are on the output one. Equal at render scale 1.0.
    float2 renderSize;
    float2 renderTexelSize;
    float2 outputTexelSize;

    // This frame's offset from a render pixel's centre, in render texels -- where the current
    // frame's sample actually landed, which is what decides how much of it each output pixel takes.
    float2 jitterTexels;

    // Output pixels per render pixel along each axis, at least one. The phases the kernel below
    // normalizes over.
    float2 subPixels;

    // Whether the two grids differ at all. Only the no-history path reads it: everything else is
    // written so that one sample per output pixel falls out as the untouched render-grid resolve.
    float resampling;

    // Reciprocal of twice the squared width of PhaseWeight's kernel, in output pixels. A knob rather
    // than a constant because narrowing it sharpens a held frame without limit while lengthening the
    // wait a moving pixel has for the phase that serves it, and that trade is judged by eye at a
    // render scale. It cannot move an image at scale 1.0: one phase weighs itself against its own
    // mean whatever the width is.
    float sampleWeightK;

    // How much of the resolved pixel comes from this frame. Zero history -- the first frame, or the
    // first after a resize -- takes the scene colour whole.
    float blendWeight;
    float historyValid;
};

ConstantBuffer<TaaResolveData> gTaaResolveData;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        VSOut vout;
        vout.uv = float2((gtid << 1) & 2, gtid & 2);
        vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

// Catmull-Rom over the 4x4 around `uv`, folded to five bilinear fetches by pairing the two inner
// taps per axis and dropping the four corners, whose weights are near zero (renormalised out).
//
// A plain bilinear fetch convolves the history with a tent every frame it lands off a texel
// centre, which under sustained motion is every frame -- the accumulation re-blurs each hop and
// the image softens until the camera stops. The negative lobes here undo that. They can also
// overshoot, so the result is only safe once the neighbourhood clamp has pulled it inside the box.
float3 SampleHistoryCatmullRom(float2 uv, float2 texel)
{
    let history = gTaaResolveData.history;
    let linearClamp = gTaaResolveData.linearSampler;

    float2 samplePos = uv / texel;
    float2 innerTexel = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - innerTexel;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    // The two inner taps collapse to one bilinear fetch placed where their weights balance.
    float2 w12 = w1 + w2;
    float2 uv0 = (innerTexel - 1.0) * texel;
    float2 uv3 = (innerTexel + 2.0) * texel;
    float2 uv12 = (innerTexel + w2 / w12) * texel;

    float3 sum = history.SampleLevel(linearClamp, float2(uv12.x, uv0.y), 0.0).rgb * (w12.x * w0.y) +
                 history.SampleLevel(linearClamp, float2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y) +
                 history.SampleLevel(linearClamp, uv12, 0.0).rgb * (w12.x * w12.y) +
                 history.SampleLevel(linearClamp, float2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y) +
                 history.SampleLevel(linearClamp, float2(uv12.x, uv3.y), 0.0).rgb * (w12.x * w3.y);

    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;

    return sum / weight;
}

float ObjectTexels(float2 uv, float2 motion, float2 texel)
{
    if (gTaaResolveData.cameraPairValid == 0.0)
    {
        return 0.0;
    }

    // The far plane is the sky or nothing: neither animates, and at infinity a translation's
    // parallax and an emptied pixel's zero would both read as motion of their own.
    const float c_FarPlane = 0.99999;

    float depth = gTaaResolveData.depth.SampleLevel(gTaaResolveData.pointSampler, uv, 0.0).r;
    if (depth >= c_FarPlane)
    {
        return 0.0;
    }

    return length(
        (motion - CameraMotion(uv, depth, gTaaResolveData.jitter, gTaaResolveData.clipToView, gTaaResolveData.viewToPrevClip)) / texel
    );
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let pointClamp = gTaaResolveData.pointSampler;
    let texel = gTaaResolveData.renderTexelSize;

    // Where this output pixel sits on the render grid, and the render pixel whose jittered sample
    // landed nearest to it -- every sample of a frame shares one offset, so the nearest is found by
    // rounding rather than searched for.
    float2 renderPos = input.uv * gTaaResolveData.renderSize;
    float2 samplePixel = floor(renderPos + gTaaResolveData.jitterTexels) + 0.5;

    // One render sample per output pixel makes the pixel's own texel the nearest sample and the
    // interpolated uv its centre already. Reconstructing that centre instead would land a rounding
    // step away, which the neighbourhood clamp and the motion discriminator both read -- and that
    // step is the entire difference between this resolve and the render-grid one it replaces.
    bool resampling = gTaaResolveData.resampling != 0.0;

    float2 sampleUv = resampling ? samplePixel * texel : input.uv;
    float2 sampleOffset = resampling ? (renderPos - samplePixel) + gTaaResolveData.jitterTexels : gTaaResolveData.jitterTexels;

    float4 current = gTaaResolveData.sceneColor.SampleLevel(pointClamp, sampleUv, 0.0);

    float2 motion = gTaaResolveData.motionVectors.SampleLevel(pointClamp, sampleUv, 0.0).xy;

    // What a surface must move by on its own before a neighbour's vector is worth borrowing: a
    // twentieth of a texel doubles no edge, and clears the reconstruction's residue on a static
    // surface -- measured bit-identical under an orbit -- by an order of magnitude.
    const float c_ObjectTexels = 0.05;

    // The 3x3 the history is admitted from: variance bounds, always. On stochastic coverage a
    // min/max box spans both populations whole and admits every dragged mixture -- the smear a
    // hashed surface trails at a distance -- where the sigma box recentres on the majority.
    // The trade-off history is in docs/taa.md.
    //
    // The same 3x3 yields the vector of whichever neighbour moves most on its own, which stands in
    // for the surface at a silhouette (docs/taa.md).
    float3 centre = RgbToYCoCg(current.rgb);
    float3 boxMin = centre;
    float3 boxMax = centre;
    float3 m1 = centre;
    float3 m2 = centre * centre;
    float2 dilatedMotion = motion;
    float dilatedObjectTexels = max(ObjectTexels(sampleUv, motion, texel), c_ObjectTexels);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            float2 tapUv = sampleUv + float2(x, y) * texel;
            float3 tap = RgbToYCoCg(gTaaResolveData.sceneColor.SampleLevel(pointClamp, tapUv, 0.0).rgb);

            boxMin = min(boxMin, tap);
            boxMax = max(boxMax, tap);
            m1 += tap;
            m2 += tap * tap;

            float2 tapMotion = gTaaResolveData.motionVectors.SampleLevel(pointClamp, tapUv, 0.0).xy;

            float tapObjectTexels = ObjectTexels(tapUv, tapMotion, texel);
            if (tapObjectTexels > dilatedObjectTexels)
            {
                dilatedObjectTexels = tapObjectTexels;
                dilatedMotion = tapMotion;
            }
        }
    }

    float2 historyUv = input.uv - dilatedMotion;

    // The sigma box is kept inside the min/max box: nine bounded samples can put mu +/- sigma
    // outside it, and outside it the clamp would admit colours the neighbourhood never produced.
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mu * mu, 0.0));
    boxMin = clamp(mu - sigma, boxMin, boxMax);
    boxMax = clamp(mu + sigma, boxMin, boxMax);

    // Reprojected off-screen: there is no history for this pixel, and wrapping the clamp sampler's
    // edge texel in would be a smear rather than an accumulation.
    bool onScreen = all(historyUv >= 0.0) && all(historyUv <= 1.0);

    // Branched, not folded into the blend weight below: the history texture is not cleared, so on the
    // first frame it holds whatever the allocation did, which may decode as NaN. `lerp(NaN, c, 1.0)`
    // is NaN -- the zero coefficient does not discard the operand -- and a NaN written into the
    // accumulation stays there for the life of the target.
    if (!onScreen || gTaaResolveData.historyValid == 0.0)
    {
        // Filtered onto the output grid rather than point-taken: there is no accumulation to build
        // the frame out of jittered samples yet, so this one frame is a plain resample.
        float3 whole = gTaaResolveData.resampling != 0.0
                           ? gTaaResolveData.sceneColor.SampleLevel(gTaaResolveData.linearSampler, input.uv, 0.0).rgb
                           : current.rgb;

        return float4(whole, 0.0);
    }

    float3 history = SampleHistoryCatmullRom(historyUv, gTaaResolveData.outputTexelSize);

    // Clamped rather than rejected: a pixel whose history is merely stale still carries subpixel
    // detail worth keeping once it is pulled inside what this frame could have produced.
    history = YCoCgToRgb(clamp(RgbToYCoCg(history), boxMin, boxMax));

    // On a grid denser than the render one, an output pixel no sample landed near this frame takes
    // almost none of it and waits for the phase that serves it.
    float sampleWeight =
        PhaseWeight(sampleOffset.x, gTaaResolveData.jitterTexels.x, gTaaResolveData.subPixels.x, gTaaResolveData.sampleWeightK) *
        PhaseWeight(sampleOffset.y, gTaaResolveData.jitterTexels.y, gTaaResolveData.subPixels.y, gTaaResolveData.sampleWeightK);

    float weight = sampleWeight * gTaaResolveData.blendWeight;

    float3 resolved = UnToneWeight(lerp(ToneWeight(history), ToneWeight(current.rgb), weight));

    return float4(resolved, 0.0);
}
