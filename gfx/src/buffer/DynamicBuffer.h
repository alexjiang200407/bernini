#pragma once

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
			return m_bufferDesc.debugName;
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
		uint32_t
		GetTotalSize() const noexcept
		{
			return m_bufferDesc.byteSize;
		}

	protected:
		void
		Init(nvrhi::DeviceHandle device, const nvrhi::BufferDesc& bufferDesc);

		[[nodiscard]]
		bool
		IsVolatile() const noexcept
		{
			return m_bufferDesc.isVolatile;
		}

	private:
		nvrhi::BufferDesc            m_bufferDesc;
		nvrhi::BufferHandle          m_buf;
		std::unique_ptr<std::byte[]> m_data;
	};
}
