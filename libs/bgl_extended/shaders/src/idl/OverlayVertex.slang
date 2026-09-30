/**
 * One corner of an overlay triangle, as a client hands it to bgl::IOverlay. `position` is in output
 * pixels, origin top-left, y down. `uv` addresses the draw's texture, (0,0) top-left. `color` is
 * RGBA8 with red in the low byte, sRGB-encoded and premultiplied by its alpha -- the form a UI
 * library hands over.
 *
 * Generated into the public <bgl/OverlayVertex.h>, so its layout must agree on every backend: the
 * float2s lead and `reserved` pads the stride to 24, the multiple of 8 Metal rounds a struct with a
 * float2 up to. Keep `reserved` zero.
 */
public struct OverlayVertex
{
    public float2 position;
    public float2 uv;
    public uint color;
    public uint reserved;
};
