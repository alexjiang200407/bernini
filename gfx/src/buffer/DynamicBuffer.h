#pragma once
#include "buffer/UpdateFrequency.h"

namespace gfx
{
	class DynamicBuffer
	{
	public:
		DynamicBuffer() noexcept            = default;
		DynamicBuffer(const DynamicBuffer&) = delete;
		DynamicBuffer(DynamicBuffer&&) noexcept;

		DynamicBuffer&
		operator=(const DynamicBuffer&) = delete;

		DynamicBuffer&
		operator=(DynamicBuffer&&) noexcept;

		void
		Update(nvrhi::CommandListHandle cmdList) noexcept;

		[[nodiscard]]
		std::string_view
		GetName() const noexcept
		{
			return m_name;
		}

		[[nodiscard]] operator nvrhi::BufferHandle() const noexcept { return m_buf; }

		[[nodiscard]] operator nvrhi::IBuffer*() const noexcept { return m_buf.Get(); }

		[[nodiscard]]
		bool
		Initialized() const noexcept;

		void
		Release() noexcept;

		// Unsafe
		[[nodiscard]] [[deprecated("Unsafe: only use for testing or inspection purposes")]]
		std::byte*
		GetRawData() noexcept
		{
			return m_data.get();
		}

		const nvrhi::BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_buf;
		}

		[[nodiscard]]
		UpdateFrequency
		GetUpdateFrequency() const noexcept
		{
			return m_updateFrequency;
		}

		[[nodiscard]]
		uint32_t
		GetTotalSize() const noexcept
		{
			return m_totalSize;
		}

	protected:
		void
		Init(
			nvrhi::DeviceHandle device,
			uint32_t            totalSize,
			UpdateFrequency     updateFreq,
			std::string_view    name);

	private:
		std::string                  m_name;
		UpdateFrequency              m_updateFrequency = UpdateFrequency::kPerFrame;
		nvrhi::BufferHandle          m_buf;
		std::unique_ptr<std::byte[]> m_data;
		uint32_t                     m_totalSize = 0u;
	};
}
