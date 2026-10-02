// Where an automatic placement's table pose sits this frame: the two frames of its rig's clip set
// that the entry its slots lean on most falls between, and how far between them, at the frame's
// time and at the previous frame's. Global frames, as the bone anim table and its soles are indexed
// by, so a reader needs neither the clip nor the record. Resolved once per placement by the camera's
// cull, which reads the record anyway, and read by every stage that draws the placement from its
// table -- one record a placement rather than one resolve of its slots per mesh-shader group, which
// cost the skinned phase more than the table saves.
//
// Indexed by the placement's MeshInstance entry and rewritten every cull.
public struct DominantFrames
{
    public uint lower;
    public uint upper;
    public float weight;

    public uint prevLower;
    public uint prevUpper;
    public float prevWeight;
};
