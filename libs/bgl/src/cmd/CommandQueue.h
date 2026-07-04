#pragma once

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandList;

	class ICommandQueue : public core::Ref
	{
	public:
		ICommandQueue() noexcept                     = default;
		ICommandQueue(const ICommandQueue&) noexcept = delete;
		ICommandQueue(ICommandQueue&&) noexcept      = delete;

		ICommandQueue&
		operator=(const ICommandQueue&) noexcept = delete;

		ICommandQueue&
		operator=(ICommandQueue&&) noexcept = delete;

		virtual uint64_t
		ExecuteCommandList(ICommandList* commandList) noexcept = 0;

		[[nodiscard]]
		virtual bool
		IsFenceComplete(uint64_t fenceValue) noexcept = 0;

		[[nodiscard]]
		virtual uint64_t
		PollCurrentFenceValue() noexcept = 0;

		[[nodiscard]]
		virtual uint64_t
		GetLastCompletedFence() const noexcept = 0;

		[[nodiscard]]
		virtual uint64_t
		GetNextFenceValue() const noexcept = 0;

		virtual void
		InsertWait(uint64_t fenceValue) noexcept = 0;

		[[nodiscard]]
		virtual void
		InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept = 0;

		[[nodiscard]]
		virtual void
		InsertWaitForQueue(ICommandQueue* otherQueue) const noexcept = 0;

		virtual void
		WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept = 0;
	};

	using CommandQueueHandle = core::SharedRef<ICommandQueue>;
}
