#pragma once
#include "cmd/CommandAllocator.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	// Metal has no command-allocator object -- command buffers come straight from the queue. This
	// exists only to satisfy the RHI shape; there is nothing to reset.
	class CommandAllocator final : public core::RefCounter<ICommandAllocator>
	{
	public:
		void
		ResetAllocator() noexcept override
		{}
	};
}
