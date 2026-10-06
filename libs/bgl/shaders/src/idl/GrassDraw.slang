import Entry;
import GrassLook;
import MeshInstance;
import TerrainGrass;

// One grass field in a view's grass list, and which GrassLook draws it: a field one visible placement
// grows, or a layer of grass on a terrain. The list's chunk references name these by index.
public struct GrassDraw
{
    public Entry<MeshInstance> mesh; // the placement of a mesh's field; null for a terrain's
    public Entry<GrassLook> look;

    // A terrain's layer, null for a mesh's field, and how many tiles the window around the camera
    // it is grown in spans along each axis -- a chunk reference names one tile of that window.
    public Entry<TerrainGrass> terrainGrass;
    public uint windowTiles;
};
