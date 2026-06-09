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
		m_LayoutMap(std::move(other.m_LayoutMap)), m_bufferDesc(std::move(other.m_bufferDesc)),
		m_buf(std::move(other.m_buf)), m_data(std::move(other.m_data)),
		m_bufferType(other.m_bufferType)
	{}

	DynamicConstantBuffer&
	DynamicConstantBuffer::operator=(DynamicConstantBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Release();
			m_LayoutMap  = std::move(other.m_LayoutMap);
			m_bufferDesc = std::move(other.m_bufferDesc);
			m_buf        = std::move(other.m_buf);
			m_data       = std::move(other.m_data);
			m_bufferType = other.m_bufferType;
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
		elementDesc.root->BuildLayoutMap(&m_LayoutMap, ctx);
		auto totalSize = align(ctx.offset, 16u);

		m_bufferType = elementDesc.bufferType;

		auto desc = nvrhi::BufferDesc{};
		desc.setByteSize(totalSize).setDebugName(elementDesc.name).setKeepInitialState(true);

		if (m_bufferType == BufferType::kPushConstant)
			desc.setIsConstantBuffer(true).setInitialState(nvrhi::ResourceStates::Common);
		else
			desc.setIsConstantBuffer(true).setInitialState(nvrhi::ResourceStates::ConstantBuffer);

		if (m_bufferType == BufferType::kVolatileConstantBuffer)
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
			THROW_GFX_ERROR("DynamicConstantBuffer::View::At", "Cannot at using a null view");

		if (key.find('.') != std::string_view::npos)
		{
			THROW_GFX_ERROR(
				"DynamicConstantBuffer::View::At",
				"Nested keys are not supported in At(): " + std::string{ key });
		}

		auto  joined = std::format("{}.{}", m_key, key);
		auto& entry  = m_parent->GetLayoutEntry(joined);
		return View{ m_totalOffset + entry.relativeOffset, m_parent, joined };
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::At(uint32_t index) const
	{
		if (IsNull())
			THROW_GFX_ERROR("DynamicConstantBuffer::View::At", "Cannot at using a null view");

		auto& entry = m_parent->GetLayoutEntry(m_key);

		if (index >= entry.count)
		{
			THROW_GFX_ERROR(
				"DynamicConstantBuffer::View::At",
				"Index out of range for entry: " + m_key);
		}

		if (!entry.IsArray())
		{
			THROW_GFX_ERROR(
				"DynamicConstantBuffer::View::At",
				"Cannot index a non-array entry: " + m_key);
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
		if (auto it = m_LayoutMap.find(name); it != m_LayoutMap.end())
		{
			return it->second;
		}

		THROW_GFX_ERROR("DynamicConstantBuffer::GetLayoutEntry", "Could not find entry");
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
			THROW_GFX_ERROR(
				"DynamicConstantBuffer::At",
				"Nested keys are not supported in At(): " + std::string{ name });
		}

		auto& entry = GetLayoutEntry(name);
		return View{ entry.relativeOffset,
			         const_cast<gfx::DynamicConstantBuffer*>(this),
			         std::string(name) };
	}

	nvrhi::BindingLayoutItem
	DynamicConstantBuffer::GetBindingLayoutItem(uint32_t slot) const noexcept
	{
		switch (m_bufferType)
		{
		case BufferType::kConstantBuffer:
			return nvrhi::BindingLayoutItem::ConstantBuffer(slot);
		case BufferType::kPushConstant:
			return nvrhi::BindingLayoutItem::PushConstants(slot, m_bufferDesc.byteSize);
		case BufferType::kVolatileConstantBuffer:
			return nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot);
		default:
			gfatal("DynamicConstantBuffer::GetBindingLayoutItem", "Unsupported buffer type");
			return nvrhi::BindingLayoutItem{};
		}
	}

	nvrhi::BindingSetItem
	DynamicConstantBuffer::GetBindingSetItem(uint32_t slot) const noexcept
	{
		switch (m_bufferType)
		{
		case BufferType::kConstantBuffer:
		case BufferType::kVolatileConstantBuffer:
			return nvrhi::BindingSetItem::ConstantBuffer(slot, m_buf);
		case BufferType::kPushConstant:
			return nvrhi::BindingSetItem::PushConstants(slot, m_bufferDesc.byteSize);
		default:
			gfatal("DynamicConstantBuffer::GetBindingLayoutItem", "Unsupported buffer type");
			return nvrhi::BindingSetItem{};
		}
	}

	void
	DynamicConstantBuffer::Update(nvrhi::CommandListHandle cmdList) noexcept
	{
		assert(m_buf.Get());
		switch (m_bufferType)
		{
		case BufferType::kConstantBuffer:
		case BufferType::kVolatileConstantBuffer:
			cmdList->writeBuffer(m_buf, m_data.get(), m_bufferDesc.byteSize);
			break;
		case BufferType::kPushConstant:
			cmdList->setPushConstants(m_data.get(), m_bufferDesc.byteSize);
			break;
		}
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
		m_LayoutMap.clear();
	}
}
