import idl.Clip;
import lib.anim.clip_playback;

// The arithmetic a 1D blend space is evaluated with: how long a member's cycle is, where a
// parameter sits between two of them, and how far a shared phase advances over one span of time.
//
// Values only, like foot_plant's -- the caller walks its own member table and passes what it read.
// What a space owns is where the parameter comes from and which members it lands between; none of
// that is here.
//
// Members are looping clips of different lengths, so "the same frame" means nothing between them.
// What is shared is a normalized phase `u`, and a member's frame is `u * ClipCycle(clip)`. The rate
// `u` advances at is the reciprocal of the weighted cycle length in seconds at the current
// parameter, so a walk blended toward a run speeds up as the parameter moves and the feet do not
// slide while it does.

// One member's cycle, in seconds: the intervals it wraps over, at its authored rate.
//
// Never zero, which is what lets every divisor below stand: a looping clip has at least two frames,
// and a space refuses a non-looping member where it is resolved.
public float MemberSeconds(Clip clip)
{
    return ClipCycle(clip) / clip.sampleRate;
}

// How far `p` sits between `a` and `b`, clamped. The span is never zero -- a space's parameters are
// refused unless they strictly increase.
public float SpanWeight(float a, float b, float p)
{
    return saturate((p - a) / (b - a));
}

// How much normalized phase elapses over `seconds`, while the weighted cycle length moves linearly
// from `dStart` to `dEnd`.
//
// The closed form of `integral of dt / D(t)` with `D` linear, which is
// `seconds * ln(dEnd / dStart) / (dEnd - dStart)`. Stepping it instead would need state carried
// between frames, and state is exactly what stops a pose being a pure function of the clock -- so
// the pose at prevTime, which is read from the same record at a different time, would stop being
// the pose that was drawn.
//
// Exact for every span, so a parameter ramp is exact while it is ramping and not merely at its
// ends, and `u` advances monotonically whatever the members' cycles: `D` is positive throughout, so
// the integrand is.
public float PhaseElapsed(float seconds, float dStart, float dEnd)
{
    let delta = dEnd - dStart;

    // The limit of the closed form as the cycle stops changing, and the whole of the answer outside
    // a ramp. Taken by a threshold rather than an equality because the quotient below loses all its
    // precision long before `delta` reaches zero.
    if (abs(delta) < 1e-6 * max(dStart, 1.0))
    {
        return seconds / dStart;
    }

    return seconds * log(dEnd / dStart) / delta;
}
