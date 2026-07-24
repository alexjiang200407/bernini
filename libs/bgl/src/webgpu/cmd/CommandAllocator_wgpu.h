#pragma once

#include "cmd/CommandAllocator.h"

namespace bgl
{
	/**
	 * A shell: WebGPU has no allocator: a command encoder owns its own memory and is discarded
	 * when it is finished. The type survives so the RHI's Open(queue, allocator) shape does.
	 */
	class CommandAllocator final : public core::RefCounter<ICommandAllocator>
	{
	public:
		CommandAllocator() noexcept = default;

		CommandAllocator(const CommandAllocator&) noexcept = delete;
		CommandAllocator(CommandAllocator&&) noexcept      = delete;

		CommandAllocator&
		operator=(const CommandAllocator&) noexcept = delete;

		CommandAllocator&
		operator=(CommandAllocator&&) noexcept = delete;

		void
		ResetAllocator() noexcept override
		{}
	};
}
