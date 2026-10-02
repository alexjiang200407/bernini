// AgentRange::sourceFirstAgent for agents that did not exist in the previous tick, spawned in
// their slots.
public static const uint c_SpawnSource = 0xFFFFFFFFu;

// TickParams::renderFirstRecord for a crowd created without a render ring: the step writes no records.
public static const uint c_NoRenderRing = 0xFFFFFFFFu;

// Threads in one group of every crowd kernel; the CPU sizes its dispatches by it, and a reduction's
// halving needs it a power of two.
public static const uint c_ThreadsPerGroup = 64u;

// An agent faces its velocity above this share of its group's speed (Group::speed, its type's
// preferred speed times its pace), and its front below it.
public static const float c_WalkingSpeedShare = 0.05;
