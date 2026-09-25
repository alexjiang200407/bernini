// One chunk to draw: the view's grass stage dispatches one amplification group per reference.
public struct GrassChunkRef
{
    public uint draw;  // into the view's GrassDraw list
    public uint chunk; // into the scene's grass chunk buffer
};
