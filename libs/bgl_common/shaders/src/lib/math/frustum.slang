// A sphere against six planes, each (n.xyz, d) with the inside on the positive side.
public bool SphereInFrustum(float4 planes[6], float3 center, float radius)
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
        {
            return false;
        }
    }
    return true;
}
