// WGSL-lowering driver for lib.anim.clip_playback.
import idl.Clip;
import idl.ClipPlayback;
import lib.anim.clip_playback;

RWStructuredBuffer<float> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    Clip clip = {};
    ClipPlayback playback = {};
    let frames = ClipFrames(clip, playback, float(id.x));
    uint f0;
    uint f1;
    float weight;
    ClipFrameIndices(clip, frames, f0, f1, weight);
    gOut[0] = float(f0 + f1) + weight;
}
