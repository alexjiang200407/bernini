import idl.Ramp;

public float RampAt(Ramp ramp, float time)
{
    if (time < ramp.start)
    {
        return ramp.from;
    }
    if (time >= ramp.end)
    {
        return ramp.to;
    }
    return lerp(ramp.from, ramp.to, (time - ramp.start) / (ramp.end - ramp.start));
}
