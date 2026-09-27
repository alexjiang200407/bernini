import idl.LodSubmeshRange;
import idl.InstanceLod;
import idl.LodLevel;
import lib.types.ComputeBuffer;

// Packs a few levels of detail the way the cull will, and one geom's submesh entry lookup, for a
// CPU comparison: whether InstanceLod::Make and bgl::UnpackInstanceLod agree on the bits, and
// whether LodSubmeshRange::Entry lands where the upload put a level's submesh.
struct Uniforms
{
    ComputeBuffer<InstanceLod> words;
    ComputeBuffer<uint> entries;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gUniforms.words[0] = InstanceLod.Make(LodLevel::kLod0, false, LodLevel::kLod0, 1.0);
    gUniforms.words[1] = InstanceLod.Make(LodLevel::kLod2, false, LodLevel::kLod5, 0.5);
    gUniforms.words[2] = InstanceLod.Make(LodLevel::kLod1, true, LodLevel::kLod0, 0.5);
    gUniforms.words[3] = InstanceLod.Make(LodLevel::kLod3, true, LodLevel::kLod1, 0.0);
    gUniforms.words[4] = InstanceLod.Make(LodLevel::kLod7, true, LodLevel::kLod6, 1.0);

    LodSubmeshRange submeshes;
    submeshes.submeshCount = 3u;
    submeshes.lodCount = 2u;
    gUniforms.entries[0] = submeshes.EntryLod0(0u);
    gUniforms.entries[1] = submeshes.Entry(LodLevel::kLod0, 2u);
    gUniforms.entries[2] = submeshes.EntryLod1(0u);
    gUniforms.entries[3] = submeshes.Entry(LodLevel::kLod1, 2u);
    gUniforms.entries[4] = submeshes.TotalCount();
}
