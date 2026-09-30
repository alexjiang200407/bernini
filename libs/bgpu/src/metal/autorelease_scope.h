#pragma once
#include <Foundation/Foundation.hpp>

namespace bgpu
{
	/**
	 * A pool that drains what the calling scope autoreleases before it returns.
	 *
	 * Every Metal entry point that makes, submits or releases an Objective-C object holds one, so an
	 * owner of the device needs no pool of its own: the renderer's thread-wide net catches nothing
	 * from here, and a compute client that has no net leaks nothing either.
	 */
	[[nodiscard]] inline NS::SharedPtr<NS::AutoreleasePool>
	ScopeAutoreleasePool() noexcept
	{
		return NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
	}
}
