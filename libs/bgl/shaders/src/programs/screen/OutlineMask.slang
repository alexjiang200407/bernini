import lib.forward.common;

// Coverage only: every surviving fragment of a selected drawable is mask, whatever its material.
// Alpha-tested and blended surfaces mask their full geometry -- selection feedback outlines the
// thing selected, not its shading.
[shader("pixel")]
float PSMain(ForwardVSOut input) : SV_Target
{
    return 1.0;
}
