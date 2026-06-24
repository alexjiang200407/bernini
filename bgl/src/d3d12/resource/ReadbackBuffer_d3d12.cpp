#include "resource/ReadbackBuffer_d3d12.h"

namespace bgl
{
	ReadbackBuffer::ReadbackBuffer(ID3D12Device* device, const ReadbackBufferDesc& desc) :
		m_ByteSize(desc.byteSize)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(desc.byteSize > 0, "Readback buffer size must be greater than zero");

		wrl::ComPtr<ID3D12Device10> device10;
		device->QueryInterface(IID_PPV_ARGS(&device10)) >> d3d12ErrChecker;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type                  = D3D12_HEAP_TYPE_READBACK;

		D3D12_RESOURCE_DESC1 resDesc = {};
		resDesc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width                = m_ByteSize;
		resDesc.Height               = 1;
		resDesc.DepthOrArraySize     = 1;
		resDesc.MipLevels            = 1;
		resDesc.Format               = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count     = 1;
		resDesc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		// Buffers have no layout under enhanced barriers, so they are created
		// LAYOUT_UNDEFINED. A readback buffer is a copy destination on first use,
		// which needs no prior barrier.
		device10->CreateCommittedResource3(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			nullptr,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&m_Buffer)) >>
			d3d12ErrChecker;

		if (!desc.debugName.empty())
		{
			std::wstring wName(desc.debugName.begin(), desc.debugName.end());
			m_Buffer->SetName(wName.c_str());
		}
	}

	ReadbackBuffer::ReadbackBuffer(ReadbackBuffer&& other) noexcept :
		m_ByteSize(other.m_ByteSize), m_Mapped(other.m_Mapped), m_Buffer(std::move(other.m_Buffer))
	{
		other.m_Mapped   = nullptr;
		other.m_ByteSize = 0;
	}

	ReadbackBuffer&
	ReadbackBuffer::operator=(ReadbackBuffer&& other) noexcept
	{
		if (this != &other)
		{
			ReleaseMapping();

			m_ByteSize = other.m_ByteSize;
			m_Mapped   = other.m_Mapped;
			m_Buffer   = std::move(other.m_Buffer);

			other.m_Mapped   = nullptr;
			other.m_ByteSize = 0;
		}
		return *this;
	}

	ReadbackBuffer::~ReadbackBuffer() noexcept { ReleaseMapping(); }

	const void*
	ReadbackBuffer::Map()
	{
		gassert(m_Buffer != nullptr, "Cannot map a null readback buffer");

		if (!m_Mapped)
		{
			// Read the whole buffer.
			D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(m_ByteSize) };
			m_Buffer->Map(0, &readRange, &m_Mapped) >> d3d12ErrChecker;
		}
		return m_Mapped;
	}

	void
	ReadbackBuffer::Unmap()
	{
		ReleaseMapping();
	}

	void
	ReadbackBuffer::ReleaseMapping() noexcept
	{
		if (m_Buffer && m_Mapped)
		{
			// The CPU wrote nothing back.
			D3D12_RANGE writeRange{ 0, 0 };
			m_Buffer->Unmap(0, &writeRange);
		}
		m_Mapped = nullptr;
	}
}
