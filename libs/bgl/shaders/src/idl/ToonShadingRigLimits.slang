// What one toon shading rig, and one view's evaluation of rigs, may hold.

// Slots one rig evaluates: an edit takes one, a mirrored edit two.
public static const uint cMaxToonShadingRigSlots = 8;

// Keys one edit may hold. Every key is weighed for every evaluated placement every frame, so this
// bounds the evaluation's per-placement cost.
public static const uint cMaxToonShadingRigKeysPerEdit = 32;

// Rigged placements one view evaluates a frame: the size of its pool of evaluated blocks. Placements
// that are visible and large enough on screen take it, up to this many; the rest shade cel only.
public static const uint cToonShadingRigPoolCapacity = 1024;
