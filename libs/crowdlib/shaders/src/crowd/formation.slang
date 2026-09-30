import crowd.idl.Group;

/**
 * Where slot `slot` of `group` stands: ranks of `frontage` front to back, each filled from the
 * front's left, the block centred on the goal and each rank across the front. crowd::SlotPosition
 * (src/formation.h) is the CPU half and must stay the same function.
 */
public float2 SlotPosition(Group group, uint slot)
{
    let rankCount = (group.agentCount + group.frontage - 1u) / group.frontage;
    let rank = slot / group.frontage;
    let file = slot % group.frontage;
    let rankLength = rank + 1u < rankCount ? group.frontage : group.agentCount - rank * group.frontage;

    let left = float2(-group.front.y, group.front.x);
    let ahead = float(rankCount - 1u) * 0.5f - float(rank);
    let across = float(rankLength - 1u) * 0.5f - float(file);
    return group.goal + group.spacing * (ahead * group.front + across * left);
}
