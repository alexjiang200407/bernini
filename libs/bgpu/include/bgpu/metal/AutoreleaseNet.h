#pragma once
#include <bgpu/api.h>

namespace bgpu
{
	/**
	 * A share of this thread's one long-lived autorelease pool: the net under every scoped pool,
	 * for a Metal object that autoreleased outside one.
	 *
	 * Pools are a per-thread stack, so two owners on one thread holding a pool each for their whole
	 * lives could only be destroyed in reverse order -- the first one's pop drains the second's, and
	 * the second's pop is then fatal. Every long-lived owner holds a share of one pool instead: the
	 * first share on a thread pushes it, the last drains it, and scoped pools nest above it as
	 * before.
	 *
	 * @pre a share is released on the thread that acquired it, as any pool is.
	 */
	class BGPU_API AutoreleaseNet
	{
	public:
		AutoreleaseNet() noexcept;
		~AutoreleaseNet() noexcept;

		AutoreleaseNet(const AutoreleaseNet&) = delete;
		AutoreleaseNet(AutoreleaseNet&&)      = delete;
		AutoreleaseNet&
		operator=(const AutoreleaseNet&) = delete;
		AutoreleaseNet&
		operator=(AutoreleaseNet&&) = delete;
	};
}
