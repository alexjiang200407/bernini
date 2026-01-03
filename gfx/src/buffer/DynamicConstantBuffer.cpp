#include "buffer/DynamicConstantBuffer.h"
#include "shader_reflect/ShaderInput.h"
#include <core/file/file.h>

namespace gfx
{
	DynamicConstantBuffer::DynamicConstantBuffer(
		nvrhi::DeviceHandle              device,
		const DynamicConstantBufferDesc& elementDesc)
	{
		Init(device, elementDesc);
	}

	DynamicConstantBuffer::DynamicConstantBuffer(
		nvrhi::DeviceHandle device,
		std::string_view    shaderPath,
		uint32_t            bindingSlot,
		uint32_t            bindingSpace,
		bool                isVolatile)
	{
		auto shaderByteCode = core::file::readFileBytes(shaderPath);
		auto desc = getDynamicConstantBufferDesc(shaderByteCode, bindingSlot, bindingSpace);
		desc.SetIsVolatile(isVolatile);

		Init(device, desc);
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

	DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::Accessor::At(std::string_view name) const
	{
		if (IsNull())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(name)",
				                "Cannot access using a null accessor" };
		}

		if (name.find('.') != std::string_view::npos)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(name)",
				                "Nested keys are not supported in At(): " + std::string{ name } };
		}

		const auto& entry = m_parent->GetLayoutEntry(m_key);
		if (!entry.groupType.All(GroupType::Struct))
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(name)",
				                "Attempted to access member of non-struct entry: " + m_key };
		}

		const std::string fullKey =
			m_key.empty() ? std::string{ name } : (m_key + "." + std::string{ name });
		const auto& childEntry = m_parent->GetLayoutEntry(fullKey);

		return Accessor{ m_totalOffset + childEntry.relativeOffset, m_parent, fullKey };
	}

	DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::Accessor::At(uint32_t index) const
	{
		if (IsNull())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(index)",
				                "Cannot index using a null accessor" };
		}

		const auto& entry = m_parent->GetLayoutEntry(m_key);

		// Must be an array to index
		if (!entry.groupType.All(GroupType::Array))
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(index)",
				                "Attempted to index non-array entry: " + m_key };
		}

		if (index >= entry.count)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::Accessor::At(index)",
				                "Index out of range for entry: " + m_key };
		}

		const uint32_t newOffset = m_totalOffset + index * entry.stride;

		return Accessor{ newOffset, m_parent, m_key };
	}

	DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::Accessor::operator[](std::string_view name) noexcept
	{
		try
		{
			return At(name);
		}
		catch (...)
		{
			return Accessor{};
		}
	}

	DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::Accessor::operator[](uint32_t idx) noexcept
	{
		try
		{
			return At(idx);
		}
		catch (...)
		{
			return Accessor{};
		}
	}

	const LayoutEntry&
	DynamicConstantBuffer::GetLayoutEntry(std::string_view name) const
	{
		if (auto it = m_layoutMap.find(name); it != m_layoutMap.end())
		{
			return it->second;
		}

		throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
			                "DynamicConstantBuffer::GetLayoutEntry",
			                "Could not find entry" };
	}

	DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::operator[](std::string_view name) noexcept
	{
		try
		{
			return At(name);
		}
		catch (...)
		{
			return Accessor{};
		}
	}

	const DynamicConstantBuffer::Accessor
	DynamicConstantBuffer::At(std::string_view name) const
	{
		if (name.find('.') != std::string_view::npos)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::At",
				                "Nested keys are not supported in At(): " + std::string{ name } };
		}

		auto& entry = GetLayoutEntry(name);
		return Accessor{ entry.relativeOffset,
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
