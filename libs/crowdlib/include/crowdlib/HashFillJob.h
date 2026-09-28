#pragma once
#include <bgpu/GpuContext.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <crowdlib/api.h>
#include <cstdint>
#include <span>

namespace crowd
{
	struct HashFillDesc
	{
		// The elements one submission writes and reads back.
		uint32_t count = 4096;
	};

	/**
	 * One compute kernel on a queue of its own, beside whatever else runs on the context's device:
	 * a D3D12 compute queue and fence, or on Metal a second command queue and a shared event. A
	 * submission fills a GPU buffer with HashFillReference(i, seed) and copies it into a readback
	 * the CPU reads once the queue's fence has passed it.
	 *
	 * Nothing here waits on another owner's queue, and nothing another owner records waits on this
	 * one: the two run concurrently wherever the GPU lets them. Single-threaded, like the renderer.
	 */
	class HashFillJob : public core::Ref
	{
	public:
		HashFillJob(const HashFillJob&) noexcept = delete;
		HashFillJob(HashFillJob&&) noexcept      = delete;

		HashFillJob&
		operator=(const HashFillJob&) noexcept = delete;

		HashFillJob&
		operator=(HashFillJob&&) noexcept = delete;

		[[nodiscard]] virtual uint32_t
		GetCount() const noexcept = 0;

		/**
		 * Records the kernel and the copy to the readback, submits them to the compute queue and
		 * signals its fence. Returns without waiting.
		 *
		 * @return the fence value the queue reaches when this submission is done.
		 * @throws std::runtime_error if InFlight(): one readback, so one submission at a time.
		 */
		virtual uint64_t
		Submit(uint32_t seed) = 0;

		/** The highest fence value submitted; 0 before the first Submit. */
		[[nodiscard]] virtual uint64_t
		GetSubmittedFence() const noexcept = 0;

		/** The fence value the compute queue has reached, read without blocking. */
		[[nodiscard]] virtual uint64_t
		GetCompletedFence() const noexcept = 0;

		[[nodiscard]] bool
		InFlight() const noexcept
		{
			return GetCompletedFence() < GetSubmittedFence();
		}

		/** Blocks until the last submission is done. */
		virtual void
		Wait() noexcept = 0;

		/**
		 * The last submission's result, GetCount() elements.
		 *
		 * The span is valid until the next Submit.
		 *
		 * @throws std::runtime_error before the first Submit, or while InFlight().
		 */
		[[nodiscard]] virtual std::span<const uint32_t>
		GetReadback() const = 0;

	protected:
		HashFillJob() noexcept = default;
	};

	using HashFillJobRef = core::SharedRef<HashFillJob>;

	/**
	 * Builds the queue, the kernel -- compiled through the context's Slang sessions, from
	 * `crowd.CSHashFill` in the staged tree -- and the buffers on the context's device.
	 *
	 * The job holds the context, and drains its queue before letting go of it.
	 *
	 * @pre ReleaseSlangSessions's, since compiling may create this thread's session.
	 * @throws std::runtime_error for a null context or a zero count, or if the kernel does not build.
	 */
	CROWD_API HashFillJobRef
	CreateHashFillJob(bgpu::GpuContextRef context, const HashFillDesc& desc = {});

	/** What the kernel writes at `index`: the CPU half of `crowd.CSHashFill`. */
	[[nodiscard]] constexpr uint32_t
	HashFillReference(uint32_t index, uint32_t seed) noexcept
	{
		const uint32_t state = (index ^ seed) * 747796405u + 2891336453u;
		const uint32_t word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}
}
