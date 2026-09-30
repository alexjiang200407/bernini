#pragma once
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <utility>

namespace bgpu
{
	/**
	 * `pool.try_allocate_and_emplace(args...)`, except that a pool sized at zero has no slots.
	 * `core::slot_vector` reads a zero size as unbounded and grows by `emplace_back`, which moves its
	 * storage out from under the resource manager's lock-free reads.
	 */
	template <typename T, typename... Args>
	[[nodiscard]] core::slot_handle
	TryAllocateBounded(core::slot_vector<T>& pool, Args&&... args)
	{
		if (pool.capacity() == 0)
			return {};
		return pool.try_allocate_and_emplace(std::forward<Args>(args)...);
	}
}
