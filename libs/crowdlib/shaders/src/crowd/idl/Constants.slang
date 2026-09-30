// AgentRange::sourceFirstAgent for agents that did not exist in the previous tick, spawned in
// their slots.
public static const uint c_SpawnSource = 0xFFFFFFFFu;

// An agent faces its velocity above this share of its group's speed (Group::speed, its type's
// preferred speed times its pace), and its front below it.
public static const float c_WalkingSpeedShare = 0.05;
