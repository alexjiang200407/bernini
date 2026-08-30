// THIS IS A FILE GENERATED FROM VatGeom.slang. DO NOT EDIT MANUALLY

import Range;
import RangeWithCount;
import Clip;

// One rig's baked clip set: the texture pair every instance of the geom fetches, and the tables
// that address it. Positions are unorm-packed in one box over every frame of every clip -- per-
// clip boxes would make samples meaningless to interpolate across clips. A texel unpacks as
// `boundsMin + texel * boundsExtent`; both w components are padding (float3 has no layout every
// backend agrees on).
public struct VatGeom
{
    public Texture2D.Handle positions; // R16G16B16A16_UNORM
    public Texture2D.Handle normals;   // R8G8B8A8_UNORM, xyz * 0.5 + 0.5; w the tangent twist, radians / 2pi + 0.5

    public float4 boundsMin;
    public float4 boundsExtent;

    public RangeWithCount<Clip> clips;

    // One entry per submesh of the geom: vertex `v` of submesh `s` is texture column
    // `columnBases[s] + v`, the same submesh-local index the vertexMap step yields.
    public Range<uint> columnBases;
};
