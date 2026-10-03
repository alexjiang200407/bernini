import bgl.MeshInstanceWriter;

// A static writer reaching for what only a skinned block offers: the module does not compile, which
// is what keeps a static block's slots from ever holding an offset.
public struct StaticWriterSettingOffset : IMeshInstanceWriter
{
    public struct Params
    {
        public float seconds;
    };

    public static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot) { block.SetPlaybackOffset(slot, params.seconds); }
};
