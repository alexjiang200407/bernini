#include "buffer/DynamicConstantBuffer.h"
#include <core/file/file.h>

namespace gfx
{
	DynamicConstantBuffer::DynamicConstantBuffer(
		nvrhi::DeviceHandle              device,
		const DynamicConstantBufferDesc& elementDesc)
	{
		Init(device, elementDesc);
	}

	DynamicConstantBuffer::DynamicConstantBuffer(DynamicConstantBuffer&& other) noexcept :
		m_layoutMap(std::move(other.m_layoutMap)), m_bufferDesc(std::move(other.m_bufferDesc)),
		m_buf(std::move(other.m_buf)), m_data(std::move(other.m_data))
	{}

	DynamicConstantBuffer&
	DynamicConstantBuffer::operator=(DynamicConstantBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Release();
			m_layoutMap  = std::move(other.m_layoutMap);
			m_bufferDesc = std::move(other.m_bufferDesc);
			m_buf        = std::move(other.m_buf);
			m_data       = std::move(other.m_data);
		}
		return *this;
	}

	void
	DynamicConstantBuffer::Init(
		nvrhi::DeviceHandle              device,
		const DynamicConstantBufferDesc& elementDesc)
	{
		assert(device.Get() != nullptr);
		assert(!Initialized() && "DynamicConstantBuffer::Init called twice");

		auto ctx = DynamicConstantBufferDesc::BuildLayoutMapContext{};
		elementDesc.root->BuildLayoutMap(&m_layoutMap, ctx);
		auto totalSize = align(ctx.offset, 16u);

		auto desc = nvrhi::BufferDesc{};
		desc.setByteSize(totalSize)
			.setIsConstantBuffer(true)
			.setDebugName(elementDesc.name)
			.setKeepInitialState(true)
			.setInitialState(nvrhi::ResourceStates::ConstantBuffer);

		if (elementDesc.isVolatile)
		{
			desc.setIsVolatile(true).setMaxVersions(16);
		}

		m_bufferDesc = desc;
		m_buf        = device->createBuffer(m_bufferDesc);
		m_data       = std::make_unique<std::byte[]>(m_bufferDesc.byteSize);
		std::memset(m_data.get(), 0, m_bufferDesc.byteSize);
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::At(std::string_view key) const
	{
		if (IsNull())
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Cannot at using a null view" };

		if (key.find('.') != std::string_view::npos)
		{
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Nested keys are not supported in At(): " + std::string{ key } };
		}

		auto  joined = std::format("{}.{}", m_key, key);
		auto& entry  = m_parent->GetLayoutEntry(joined);
		return View{ m_totalOffset + entry.relativeOffset, m_parent, joined };
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::At(uint32_t index) const
	{
		if (IsNull())
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Cannot at using a null view" };

		auto& entry = m_parent->GetLayoutEntry(m_key);

		if (index >= entry.count)
		{
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Index out of range for entry: " + m_key };
		}

		if (!entry.IsArray())
		{
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Cannot index a non-array entry: " + m_key };
		}

		return View{ m_totalOffset + index * entry.stride, m_parent, m_key };
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::operator[](std::string_view name) noexcept
	{
		try
		{
			return At(name);
		}
		catch (...)
		{
			return View{};
		}
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::operator[](uint32_t idx) noexcept
	{
		try
		{
			return At(idx);
		}
		catch (...)
		{
			return View{};
		}
	}

	const LayoutEntry&
	DynamicConstantBuffer::GetLayoutEntry(std::string_view name) const
	{
		if (auto it = m_layoutMap.find(name); it != m_layoutMap.end())
		{
			return it->second;
		}

		throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
			                "DynamicConstantBuffer::GetLayoutEntry",
			                "Could not find entry" };
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::operator[](std::string_view name) noexcept
	{
		try
		{
			return At(name);
		}
		catch (...)
		{
			return View{};
		}
	}

	const DynamicConstantBuffer::View
	DynamicConstantBuffer::At(std::string_view name) const
	{
		if (name.find('.') != std::string_view::npos)
		{
			throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::At",
				                "Nested keys are not supported in At(): " + std::string{ name } };
		}

		auto& entry = GetLayoutEntry(name);
		return View{ entry.relativeOffset,
			         const_cast<gfx::DynamicConstantBuffer*>(this),
			         std::string(name) };
	}

	nvrhi::BindingLayoutItem
	DynamicConstantBuffer::GetBindingLayoutItem(uint32_t slot) const noexcept
	{
		if (IsVolatile())
		{
			return nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot);
		}
		return nvrhi::BindingLayoutItem::ConstantBuffer(slot);
	}

	nvrhi::BindingSetItem
	DynamicConstantBuffer::GetBindingSetItem(uint32_t slot) const noexcept
	{
		return nvrhi::BindingSetItem::ConstantBuffer(slot, GetBufferHandle());
	}

	void
	DynamicConstantBuffer::Update(nvrhi::CommandListHandle cmdList) noexcept
	{
		assert(m_buf.Get());
		cmdList->writeBuffer(m_buf, m_data.get(), m_bufferDesc.byteSize);
	}

	bool
	DynamicConstantBuffer::Initialized() const noexcept
	{
		return m_buf != nullptr;
	}

	void
	DynamicConstantBuffer::Release() noexcept
	{
		m_data.reset();
		m_bufferDesc = {};
		m_buf.Reset();
		m_layoutMap.clear();
	}
}
