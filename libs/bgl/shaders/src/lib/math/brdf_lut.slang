// The split-sum BRDF integral: the environment-independent half of specular IBL.
//
// Integrating a *white* environment leaves a function of only (dot(N,V), roughness), and Fresnel is
// linear in F0, so the result factors into a scale and a bias on F0. That is what PbrShading samples:
// specular = prefiltered * (F0 * A + B).

// Enough that the estimate is converged at the resolution written; this runs once per device.
static const uint c_SampleCount = 1024u;

// Van der Corput radical inverse, base 2 -- the second dimension of a Hammersley set.
float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), RadicalInverseVdC(i));
}

// A half-vector drawn from the GGX distribution, in a tangent frame whose normal is +Z.
float3 ImportanceSampleGGX(float2 Xi, float roughness)
{
    const float a = roughness * roughness;

    const float phi = 2.0 * 3.14159265359 * Xi.x;

    // Saturated before the root, not after. As a approaches 0 both halves of the ratio go to zero
    // and it lands above 1, which would take sinTheta through the root of a negative -- and the
    // mirror row, where the sampling is exact, is the one row that would come back NaN.
    const float cosTheta = sqrt(saturate((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y)));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Smith geometry with the IBL remapping k = a / 2, which differs from the direct-light (a+1)^2 / 8.
// Squaring a here instead costs most of the masking and the integral then gains energy at grazing
// incidence, reaching 8 in the first column rather than staying under one.
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    const float a = roughness * roughness;
    const float k = a / 2.0;

    const float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    const float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

public float2 IntegrateBRDF(float NdotV, float roughness)
{
    // Isotropic, so the view may be placed anywhere in the XZ plane at this elevation.
    const float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float scale = 0.0;
    float bias = 0.0;

    for (uint i = 0u; i < c_SampleCount; ++i)
    {
        const float3 H = ImportanceSampleGGX(Hammersley(i, c_SampleCount), roughness);
        const float3 L = 2.0 * dot(V, H) * H - V;

        const float NdotL = L.z;
        if (NdotL <= 0.0)
            continue;

        const float NdotH = max(H.z, 0.0);
        const float VdotH = max(dot(V, H), 0.0);

        // The pdf and the D term cancel, leaving G weighted by the change of variables.
        const float visibility = (GeometrySmith(NdotV, NdotL, roughness) * VdotH) / (NdotH * NdotV);
        const float fresnel = pow(1.0 - VdotH, 5.0);

        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }

    return float2(scale, bias) / float(c_SampleCount);
}
