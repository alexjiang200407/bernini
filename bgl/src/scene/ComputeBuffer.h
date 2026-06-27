#pragma once
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
		ComputeBuffer(ComputeBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		void
		Init(ComputeBufferDesc desc, ResourceManagerHandle resourceManager) noexcept;

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
		Release(uint64_t fenceValue, bool deferred = true) noexcept
		{
			if (!m_Handle.IsNull())
			{
				m_ResourceManager->DestroyBuffer(m_Handle, fenceValue, deferred);
				m_Handle = {};
			}
		}

	private:
		ComputeBufferDesc     m_Desc;
		ResourceManagerHandle m_ResourceManager;
		BufferHandle          m_Handle;
	};
}
