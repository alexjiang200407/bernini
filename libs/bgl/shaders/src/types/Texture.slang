// What a bindless texture needs beyond the built-ins, reached through Texture2D.Handle /
// TextureCube.Handle like any other method: member lookup on a handle resolves against the resource
// type, so extending the handle itself would compile and never be found.

public extension Texture2D
{
    // A texel by integer coordinate, no sampler in the loop. What a fetch keyed on an exact texel
    // (a VAT column) wants -- and what a stage with no sampler bound can still do.
    public float4 Load(uint2 coord, uint mip = 0)
    {
        return this.Load(int3(coord, mip));
    }

    // Mip 0 texel counts. A caller relating a UV footprint to texels needs the real size;
    // hardcoding one would silently mis-scale the moment the asset changed.
    public float2 GetDimensions()
    {
        uint w;
        uint h;
        uint levels;
        this.GetDimensions(0, w, h, levels);
        return float2(w, h);
    }
}

public extension TextureCube
{
    // Texels along one face edge of mip 0. A caller picking its own LOD needs this to know what mip 0
    // already resolves; hardcoding it would silently mis-scale the moment the asset changed size.
    public uint CubeFaceTexels()
    {
        uint w;
        uint h;
        uint levels;
        this.GetDimensions(0, w, h, levels);
        return w;
    }
}
