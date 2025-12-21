#include "buffer/DynamicConstantBuffer.h"
#include "math/util.h"
#include <core/OrderedMap.h>

namespace gfx
{
	DynamicConstantBufferDesc::Node::~Node() {}

	class DynamicConstantBufferDesc::StructNode : public DynamicConstantBufferDesc::Node
	{
	private:
		using StructChildren = core::OrderedMap<
			std::string,
			std::unique_ptr<Node>,
			core::str::StringViewHash,
			core::str::StringViewEq>;

	public:
		StructNode(std::string_view name) : m_name{ name } {}

		void
		AddNode(std::string_view path, std::unique_ptr<Node>&& toAdd) override
		{
			auto [head, tail] = core::str::splitOnce(path, ".");

			if (tail.empty())
			{
				if (m_children.Contains(head))
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc::StructNode::AddNode",
						                "Node already exists: " + std::string{ head } };
				}
				m_children.Emplace(head, std::move(toAdd));
			}
			else
			{
				auto* child = m_children.Find(head);
				if (!child)
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc::StructNode::AddNode",
						                "Node not found: " + std::string{ head } };
				}
				(*child)->AddNode(tail, std::move(toAdd));
			}
		}

		void
		BuildDynamicBufferDesc(DynamicBufferDesc& desc, BuildDynamicBufferContext& ctx)
			const noexcept override
		{
			// Constant buffers need to start on a 16-byte boundary
			ctx.alignNext = true;
			for (const auto& child : m_children)
			{
				child->BuildDynamicBufferDesc(desc, ctx);
			}
		}

	private:
		std::string    m_name;
		StructChildren m_children;
	};

	class DynamicConstantBufferDesc::ElementNode : public DynamicConstantBufferDesc::Node
	{
	public:
		ElementNode(std::string_view name, ElementType type) : m_name{ name }, m_type{ type } {};

		void
		AddNode(std::string_view, std::unique_ptr<Node>&&) override
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::ElementNode::AddNode",
				                "Cannot add node from ElementNode" };
		}

		void
		BuildDynamicBufferDesc(DynamicBufferDesc& desc, BuildDynamicBufferContext& ctx)
			const noexcept
		{
			const uint32_t elemSize    = sizeOfElementType(m_type);
			const uint32_t offsetInReg = ctx.offset & 15u;
			const uint32_t remaining   = 16u - offsetInReg;

			uint32_t padding = 0;

			// Small types (<=4 bytes) cannot cross 4-byte boundaries
			if (elemSize <= 4 && (ctx.offset & 3u) + elemSize > 4)
			{
				padding = 4 - (ctx.offset & 3u);
				ctx.offset += padding;
			}

			// If element would cross 16-byte boundary, align to next 16-byte boundary
			if (elemSize > remaining || ctx.alignNext)
			{
				const uint32_t alignedOffset = align(ctx.offset, 16u);
				padding                      = alignedOffset - ctx.offset;
				ctx.offset                   = alignedOffset;
				ctx.alignNext                = false;
			}

			desc.AddElement(m_name, m_type, padding);
			ctx.offset += elemSize;
		}

	private:
		std::string m_name;
		ElementType m_type;
	};

	class DynamicConstantBufferDesc::ElementArrayNode : public DynamicConstantBufferDesc::Node
	{
	public:
		ElementArrayNode(std::string_view name, uint32_t count, ElementType type) noexcept :
			m_count{ count }, m_type{ type }
		{}

		void
		AddNode(std::string_view, std::unique_ptr<Node>&&) override
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::ElementNode::AddNode",
				                "Cannot add node from ElementNode" };
		}

		void
		BuildDynamicBufferDesc(DynamicBufferDesc&, BuildDynamicBufferContext&)
			const noexcept override
		{}

	private:
		uint32_t    m_count;
		ElementType m_type;
	};

	class DynamicConstantBufferDesc::StructArrayNode : public DynamicConstantBufferDesc::Node
	{
	public:
		StructArrayNode(std::string_view name, uint32_t count) noexcept :
			m_name{ name }, m_count{ count }
		{}

		void
		AddNode(std::string_view path, std::unique_ptr<Node>&& toAdd) override
		{
			m_structNode.AddNode(path, std::move(toAdd));
		}

		void
		BuildDynamicBufferDesc(DynamicBufferDesc&, BuildDynamicBufferContext&)
			const noexcept override
		{}

	private:
		std::string m_name;
		StructNode  m_structNode{ "" };
		uint32_t    m_count;
	};

	DynamicConstantBufferDesc::DynamicConstantBufferDesc()
	{
		root = std::make_unique<StructNode>("root");
	}

	DynamicConstantBuffer::DynamicConstantBuffer(
		nvrhi::DeviceHandle              device,
		const DynamicConstantBufferDesc& elementDesc) :
		DynamicBuffer{ elementDesc.ToDynamicBufferDesc(), 1 }
	{
		auto& parentDesc = GetDesc();

		auto bufferDesc = nvrhi::BufferDesc{};
		bufferDesc.setByteSize(parentDesc.GetTotalSize())
			.setIsConstantBuffer(true)
			.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
			.setKeepInitialState(false)
			.setDebugName(std::string{ GetName() });

		if (parentDesc.GetUpdateFrequency() == DynamicBufferDesc::UpdateFrequency::kPerFrame)
		{
			static constexpr auto maxVersions = 16u;
			bufferDesc.setIsVolatile(true);
			bufferDesc.setMaxVersions(maxVersions);
		}

		m_buf = device->createBuffer(bufferDesc);
	}

	DynamicBufferItem::View
	DynamicConstantBuffer::operator[](std::string_view name) noexcept
	{
		return DynamicBuffer::operator[](0)[name];
	}

	DynamicBufferItem::View
	DynamicConstantBuffer::At(std::string_view name)
	{
		return operator[](0).At(name);
	}

	nvrhi::BindingLayoutItem
	DynamicConstantBuffer::GetBindingLayoutItem(uint32_t slot) const noexcept
	{
		if (GetDesc().GetUpdateFrequency() == DynamicBufferDesc::UpdateFrequency::kPerDraw)
		{
			return nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot);
		}
		return nvrhi::BindingLayoutItem::ConstantBuffer(slot);
	}

	nvrhi::BindingSetItem
	DynamicConstantBuffer::GetBindingSetItem(uint32_t slot) const noexcept
	{
		return nvrhi::BindingSetItem::ConstantBuffer(slot, m_buf);
	}

	DynamicBufferDesc
	DynamicConstantBufferDesc::ToDynamicBufferDesc() const
	{
		auto desc = DynamicBufferDesc{};
		desc.SetName(name).SetAlignment(16);

		auto ctx = BuildDynamicBufferContext{};
		root->BuildDynamicBufferDesc(desc, ctx);

		return desc;
	}

	DynamicConstantBufferDesc&
	DynamicConstantBufferDesc::AddStruct(std::string_view name)
	{
		if (name.empty())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::AddStruct",
				                "Struct name cannot be empty" };
		}
		root->AddNode(name, std::make_unique<StructNode>(name));
		return *this;
	}

	DynamicConstantBufferDesc&
	DynamicConstantBufferDesc::AddElementArray(
		std::string_view name,
		ElementType      type,
		uint32_t         count)
	{
		if (type == ElementType::kInvalid)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::AddElementArray",
				                "Invalid ElementType specified" };
		}

		if (name.empty())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::AddElementArray",
				                "Element array name cannot be empty" };
		}

		root->AddNode(name, std::make_unique<ElementArrayNode>(name, count, type));
		return *this;
	}

	DynamicConstantBufferDesc&
	DynamicConstantBufferDesc::AddElement(std::string_view name, ElementType format)
	{
		if (name.empty())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::AddElement",
				                "Element name cannot be empty" };
		}

		root->AddNode(name, std::make_unique<ElementNode>(name, format));
		return *this;
	}
}
