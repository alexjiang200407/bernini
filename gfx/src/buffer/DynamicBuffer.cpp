#include "buffer/DynamicBuffer.h"

namespace gfx
{

	void
	DynamicBuffer::Init(nvrhi::DeviceHandle device, const nvrhi::BufferDesc& bufferDesc)
	{
		assert(device.Get() != nullptr);
		assert(!Initialized() && "DynamicBuffer::Init called twice");

		m_bufferDesc = bufferDesc;
		m_buf        = device->createBuffer(bufferDesc);
		m_data       = std::make_unique<std::byte[]>(m_bufferDesc.byteSize);
		std::memset(m_data.get(), 0, m_bufferDesc.byteSize);
	}

	DynamicBuffer::DynamicBuffer(DynamicBuffer&& other) noexcept :
		m_buf(std::move(other.m_buf)), m_data(std::move(other.m_data)),
		m_bufferDesc{ std::move(other.m_bufferDesc) }
	{}

	DynamicBuffer&
	DynamicBuffer::operator=(DynamicBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Release();

			m_buf        = std::move(other.m_buf);
			m_data       = std::move(other.m_data);
			m_bufferDesc = std::move(other.m_bufferDesc);
		}
		return *this;
	}

	void
	DynamicBuffer::Update(nvrhi::CommandListHandle cmdList) noexcept
	{
		assert(m_buf.Get());
		cmdList->writeBuffer(m_buf, m_data.get(), m_bufferDesc.byteSize);
	}

	bool
	DynamicBuffer::Initialized() const noexcept
	{
		return m_buf != nullptr;
	}

	void
	DynamicBuffer::Release() noexcept
	{
		m_data.reset();
		m_bufferDesc = {};
		m_buf.Reset();
	}
}
