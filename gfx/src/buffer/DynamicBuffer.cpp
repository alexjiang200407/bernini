#include "buffer/DynamicBuffer.h"

namespace gfx
{

	void
	DynamicBuffer::Init(
		nvrhi::DeviceHandle device,
		uint32_t            totalSize,
		UpdateFrequency     updateFreq,
		std::string_view    name)
	{
		assert(device.Get() != nullptr);
		assert(!Initialized() && "DynamicBuffer::Init called twice");

		m_name          = std::string(name);
		auto bufferDesc = nvrhi::BufferDesc{};
		bufferDesc.setByteSize(totalSize)
			.setIsVertexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setKeepInitialState(false)
			.setDebugName(m_name);

		m_buf             = device->createBuffer(bufferDesc);
		m_updateFrequency = updateFreq;
		m_totalSize       = totalSize;
		m_data            = std::make_unique<std::byte[]>(m_totalSize);
		std::memset(m_data.get(), 0, m_totalSize);
	}

	DynamicBuffer::DynamicBuffer(DynamicBuffer&& other) noexcept :
		m_buf(std::move(other.m_buf)), m_data(std::move(other.m_data)),
		m_totalSize(std::exchange(other.m_totalSize, 0)),
		m_updateFrequency(other.m_updateFrequency), m_name(std::move(other.m_name))
	{}

	DynamicBuffer&
	DynamicBuffer::operator=(DynamicBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Release();

			m_buf             = std::move(other.m_buf);
			m_data            = std::move(other.m_data);
			m_totalSize       = std::exchange(other.m_totalSize, 0);
			m_updateFrequency = std::exchange(other.m_updateFrequency, UpdateFrequency::kPerFrame);
			m_name            = std::move(other.m_name);
		}
		return *this;
	}

	void
	DynamicBuffer::Update(nvrhi::CommandListHandle cmdList) noexcept
	{
		assert(m_buf.Get());
		cmdList->writeBuffer(m_buf, m_data.get(), m_totalSize);
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
		m_totalSize = 0;
		m_buf.Reset();
	}
}
