// One bit of a placement's flags word (MeshInstance.flags). A zeroed word is a placement's default
// behaviour, so every bit names an exception to it -- never a requirement a writer must remember to
// set.
public enum InstanceFlag : uint32_t
{
    // Drawn by no pass of any view: culled before the counting sort, and absent from the blob-shadow
    // and selection-outline lists. Still posed and still ticked, so unhiding shows the pose it is in.
    kHidden = 1,
}
;
