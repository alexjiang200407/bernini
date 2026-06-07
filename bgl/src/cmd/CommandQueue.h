#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandList;

	class ICommandQueue : public core::Ref
	{
	public:
		virtual uint64_t
		ExecuteCommandList(ICommandList* commandList) = 0;

		[[nodiscard]]
		virtual bool
		IsFenceComplete(uint64_t fenceValue) = 0;

		[[nodiscard]]
		virtual uint64_t
		PollCurrentFenceValue() = 0;

		[[nodiscard]]
		virtual uint64_t
		GetLastCompletedFence() const = 0;

		[[nodiscard]]
		virtual uint64_t
		GetNextFenceValue() const = 0;

		virtual void
		InsertWait(uint64_t fenceValue) = 0;

		[[nodiscard]]
		virtual void
		InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const = 0;

		[[nodiscard]]
		virtual void
		InsertWaitForQueue(ICommandQueue* otherQueue) const = 0;

		virtual void
		WaitForFenceCPUBlocking(uint64_t fenceValue) = 0;
	};

	using CommandQueueHandle = core::SharedRef<ICommandQueue>;
}
