#pragma once
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "scene/GrowableGpuBuffer.h"

namespace bgl
{
	/**
	 * A ComputeBuffer is a GPU-only structured buffer that compute shaders fill via
	 * UAV writes.
	 */
	class ComputeBuffer
	{
	public:
		ComputeBuffer() noexcept = default;
		ComputeBuffer(ComputeBufferDesc desc, ResourceManagerRef resourceManager)
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		ComputeBuffer(const ComputeBuffer&)     = delete;
		ComputeBuffer(ComputeBuffer&&) noexcept = default;

		ComputeBuffer&
		operator=(const ComputeBuffer&) = delete;

		ComputeBuffer&
		operator=(ComputeBuffer&&) noexcept = default;

		void
		Init(ComputeBufferDesc desc, ResourceManagerRef resourceManager);

		/**
		 * Reallocates at `newCount` elements, discarding the contents: this is per-frame scratch
		 * that its producing pass overwrites, so there is nothing to carry forward.
		 *
		 * @throws std::runtime_error if the device cannot allocate; the buffer is left intact.
		 */
		void
		Resize(uint32_t newCount);

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Storage.IsInitialized();
		}

		[[nodiscard]] const ComputeBufferDesc&
		GetDesc() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return m_Desc;
		}

		// Re-read every frame: Resize mints a new handle and retires the old one (see
		// GrowableGpuBuffer), so a cached descriptor index goes stale.
		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return m_Storage.GetHandle();
		}

		// Retires the resources a Resize superseded. Nothing is copied forward.
		void
		Update(ICommandList* cmdList)
		{
			m_Storage.FlushGrowth(cmdList);
		}

		[[nodiscard]] uint64_t
		ByteSize() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return static_cast<uint64_t>(m_Desc.initialCount) * m_Desc.elementSize;
		}

		void
		Clear(ICommandList* cmd) noexcept
		{
			gassert(cmd != nullptr, "Command list cannot be null");
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");

			const auto zeros = std::vector<std::byte>(ByteSize(), std::byte{ 0 });
			cmd->WriteBuffer(m_Storage.GetHandle(), zeros.data(), zeros.size());
		}

		void
		Release(bool deferred = true) noexcept
		{
			m_Storage.Release(deferred);
		}

	private:
		ComputeBufferDesc m_Desc;
		GrowableGpuBuffer m_Storage;
	};

	template <typename T>
	struct is_compute_buffer : std::false_type
	{};

	template <>
	struct is_compute_buffer<ComputeBuffer> : std::true_type
	{};

	template <typename T>
	inline constexpr bool is_compute_buffer_v = is_compute_buffer<std::decay_t<T>>::value;
}
