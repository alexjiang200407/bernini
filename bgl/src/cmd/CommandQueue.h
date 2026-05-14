#pragma once
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	class CommandList;

	class CommandQueueImpl;
	class CommandQueue : public core::RefCountPImpl<CommandQueueImpl>
	{
	public:
		uint64_t
		ExecuteCommandList(const CommandList& commandList) const;

		[[nodiscard]]
		bool
		IsFenceComplete(uint64_t fenceValue) const;

		[[nodiscard]]
		uint64_t
		PollCurrentFenceValue() const;

		[[nodiscard]]
		uint64_t
		GetLastCompletedFence() const;

		[[nodiscard]]
		uint64_t
		GetNextFenceValue() const;

		[[noreturn]]
		void
		InsertWait(uint64_t fenceValue);

		[[nodiscard]]
		void
		InsertWaitForQueueFence(CommandQueue cq, uint64_t fenceValue);

		[[nodiscard]]
		void
		InsertWaitForQueue(CommandQueue otherQueue);

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue);

		friend class DeviceImpl;
	};
}
