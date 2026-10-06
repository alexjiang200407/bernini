import idl.Terrain;

// A terrain's heightfield read at a world position: the one place anything samples one, so the
// stage that draws it and whatever stands on it later -- a planted foot, a crowd's slope cost --
// read the same ground. The samples are a unorm texture the terrain's record decodes; sample
// (x, z) sits at the centre of texel (x, z), so a grid vertex reads its own sample exactly.

// The texture coordinate of world `xz` on a terrain's height texture.
public float2 TerrainUv(Terrain terrain, float2 xz)
{
    let cell = (xz - terrain.originAndCellSize.xz) / terrain.originAndCellSize.w;
    return (cell + 0.5) / float2(terrain.samplesX, terrain.samplesZ);
}

// The world position of sample (x, z) on the ground grid, before its height.
public float2 TerrainSampleXz(Terrain terrain, uint2 sample)
{
    return terrain.originAndCellSize.xz + float2(sample) * terrain.originAndCellSize.w;
}

// The height at world `xz`, bilinear between samples and clamped at the field's edge.
public float TerrainHeightAt(Terrain terrain, Texture2D.Handle heights, SamplerState.Handle clampSampler, float2 xz)
{
    let share = heights.SampleLevel(clampSampler, TerrainUv(terrain, xz), 0.0).r;
    return terrain.originAndCellSize.y + share * terrain.heightRangeAndPixelsPerCell.x;
}

// The unit normal at world `xz`, and the unit tangent along +x on the surface there, from central
// differences one cell either way. At the edge both sides clamp to the edge sample, which flattens
// the frame rather than tilting it.
public void TerrainFrameAt(
    Terrain terrain,
    Texture2D.Handle heights,
    SamplerState.Handle clampSampler,
    float2 xz,
    out float3 normal,
    out float3 tangent
)
{
    let cell = terrain.originAndCellSize.w;
    let left = TerrainHeightAt(terrain, heights, clampSampler, xz - float2(cell, 0.0));
    let right = TerrainHeightAt(terrain, heights, clampSampler, xz + float2(cell, 0.0));
    let down = TerrainHeightAt(terrain, heights, clampSampler, xz - float2(0.0, cell));
    let up = TerrainHeightAt(terrain, heights, clampSampler, xz + float2(0.0, cell));
    normal = normalize(float3(left - right, 2.0 * cell, down - up));
    tangent = normalize(float3(2.0 * cell, right - left, 0.0));
}

public float3 TerrainNormalAt(Terrain terrain, Texture2D.Handle heights, SamplerState.Handle clampSampler, float2 xz)
{
    float3 normal;
    float3 tangent;
    TerrainFrameAt(terrain, heights, clampSampler, xz, normal, tangent);
    return normal;
}
