#pragma once

namespace crowd
{
	/**
	 * The kinematics every agent of a group shares, in world units, seconds and radians. A group has
	 * exactly one type. Every field must be positive: a crowd refuses a type with one that is not.
	 */
	struct AgentType
	{
		float radius          = 0.0f;
		float maxSpeed        = 0.0f;
		float maxAcceleration = 0.0f;
		float maxTurnRate     = 0.0f;
		float mass            = 0.0f;
	};
}
