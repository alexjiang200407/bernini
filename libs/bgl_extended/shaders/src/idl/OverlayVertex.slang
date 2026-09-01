// THIS IS A FILE GENERATED FROM OverlayVertex.slang. DO NOT EDIT MANUALLY

/**
 * One corner of an overlay triangle, as bgl::OverlayVertex hands it over: output pixels, a texture
 * coordinate, and an RGBA8 color (red in the low byte) that is sRGB-encoded and premultiplied.
 *
 * The float2s lead and the color carries a pad so the scalar layout D3D12 reads and the natural
 * layout Metal gives a device struct agree on every offset and on the 24-byte stride.
 */
public struct OverlayVertex
{
    public float2 position;
    public float2 uv;
    public uint color;
    public uint reserved;
};
