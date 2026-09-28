// Integer hashing for values that must look random but be reproducible: a grass blade's shape from
// its index, a dissolve's coverage from a pixel and a frame. lowbias32 (Chris Wellons), a full
// avalanche in two multiplies.

public uint HashUint(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// Uniform in [0, 1): the hash's top 24 bits, which a float holds exactly.
public float HashFloat(uint x)
{
    return float(HashUint(x) >> 8u) * (1.0 / 16777216.0);
}
