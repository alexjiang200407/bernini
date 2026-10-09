import idl.ToonShadingRigBlock;

// What only the toon model's programs read: the view's evaluated toon shading rig blocks. A cbuffer
// of its own so the programs that never import it -- every engine material's -- carry none of it.
public struct ToonData
{
    // Named by a toon character draw's vertices (ToonVSOut::toonShadingRigSlot).
    public RWStructuredBuffer<ToonShadingRigBlock>.Handle toonShadingRigBlocks;
};

public ConstantBuffer<ToonData> toonData;
