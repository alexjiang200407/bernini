// One module carrying a mesh entry alongside the vertex + pixel pair, mirroring how the forward
// shaders keep amp/mesh (D3D12) and compute/vertex (WebGPU) entries side by side.
//
// Slang validates every entry point in a module against the compilation target at load time -- not
// lazily per linked entry -- so a [shader("mesh")] entry reaching a WGSL session is rejected even
// when nothing links it (E36107: features unavailable in the '_mesh' stage for 'wgsl'). The mesh /
// amplification entries must therefore be excluded from the WGSL compile with a #ifdef; BGL_WGSL is
// defined on the runtime WGSL session (and the offline compile_shader arm), so this guards on it.

struct VSOut
{
    float4 position : SV_Position;
};

#ifndef BGL_WGSL
[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void MSMain(OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
}
#endif

[shader("vertex")]
VSOut VSMain(uint index: SV_VertexID)
{
    float2 corners[3] = {
        float2(0.0, 0.8),
        float2(-0.8, -0.8),
        float2(0.8, -0.8),
    };

    VSOut output;
    output.position = float4(corners[index], 0.0, 1.0);
    return output;
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
