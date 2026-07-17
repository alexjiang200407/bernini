#pragma once
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

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
		ComputeBuffer(ComputeBufferDesc desc, ResourceManagerRef resourceManager) noexcept
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		void
		Init(ComputeBufferDesc desc, ResourceManagerRef resourceManager) noexcept;

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return !m_Handle.IsNull();
		}

		[[nodiscard]] const ComputeBufferDesc&
		GetDesc() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return m_Desc;
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return m_Handle;
		}

		[[nodiscard]] uint64_t
		ByteSize() const noexcept
		{
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");
			return static_cast<uint64_t>(m_Desc.maxCount) * m_Desc.elementSize;
		}

		void
		Clear(ICommandList* cmd) noexcept
		{
			gassert(cmd != nullptr, "Command list cannot be null");
			gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");

			const auto zeros = std::vector<std::byte>(ByteSize(), std::byte{ 0 });
			cmd->WriteBuffer(m_Handle, zeros.data(), zeros.size());
		}

		void
		Release(uint64_t fenceValue, bool deferred = true) noexcept
		{
			if (!m_Handle.IsNull())
			{
				m_ResourceManager->DestroyBuffer(m_Handle, fenceValue, deferred);
				m_Handle = {};
			}

			m_ResourceManager.Reset();
		}

	private:
		ComputeBufferDesc  m_Desc;
		ResourceManagerRef m_ResourceManager;
		BufferHandle       m_Handle;
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
