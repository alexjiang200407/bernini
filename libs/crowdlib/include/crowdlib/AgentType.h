#pragma once

namespace crowd
{
	/**
	 * What every agent of a group shares, in world units and seconds: a disc of `radius` that walks
	 * at `preferredSpeed`, never faster than `maxSpeed`, and gives way to heavier agents in
	 * proportion to `mass`. A group has exactly one type. Every field must be positive and finite,
	 * and `preferredSpeed` at most `maxSpeed`: a crowd refuses a type that breaks either.
	 */
	struct AgentType
	{
		float radius         = 0.0f;
		float preferredSpeed = 0.0f;
		float maxSpeed       = 0.0f;
		float mass           = 0.0f;
	};
}
