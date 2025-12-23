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
		BuildLayoutMap(LayoutMap* layoutMap, BuildLayoutMapContext& ctx) const noexcept override
		{
			if (m_children.Empty())
				return;

			// Constant buffers need to start on a 16-byte boundary
			ctx.offset           = align(ctx.offset, 16u);
			auto parentOffsetCpy = ctx.parentOffset;
			ctx.parentOffset     = ctx.offset;

			auto prevOffset = ctx.offset;
			for (const auto& child : m_children)
			{
				child->BuildLayoutMap(layoutMap, ctx);
			}

			ctx.parentOffset = parentOffsetCpy;

			auto elemSize = ctx.offset - prevOffset;

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{
						.relativeOffset = prevOffset - ctx.parentOffset,
						.elemSize       = elemSize,
						.stride         = 0,
						.count          = 1,
					});
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
		BuildLayoutMap(LayoutMap* layoutMap, BuildLayoutMapContext& ctx) const noexcept
		{
			auto elemSize    = sizeOfElementType(m_type);
			auto offsetInReg = ctx.offset & 15u;
			auto remaining   = 16u - offsetInReg;

			uint32_t padding = 0;

			// Small types (<=4 bytes) cannot cross 4-byte boundaries
			if (elemSize <= 4 && (ctx.offset & 3u) + elemSize > 4)
			{
				padding = 4 - (ctx.offset & 3u);
				ctx.offset += padding;
			}

			// If element would cross 16-byte boundary, align to next 16-byte boundary
			if (elemSize > remaining)
			{
				const uint32_t alignedOffset = align(ctx.offset, 16u);
				padding                      = alignedOffset - ctx.offset;
				ctx.offset                   = alignedOffset;
			}

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{
						.relativeOffset = ctx.offset - ctx.parentOffset,
						.elemSize       = elemSize,
						.stride         = 0,
						.count          = 1,
					});
			}
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
			m_name{ name }, m_count{ count }, m_type{ type }
		{}

		void
		AddNode(std::string_view, std::unique_ptr<Node>&&) override
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::ElementNode::AddNode",
				                "Cannot add node from ElementNode" };
		}

		void
		BuildLayoutMap(LayoutMap* layoutMap, BuildLayoutMapContext& ctx) const noexcept override
		{
			auto elemSize    = sizeOfElementType(m_type);
			auto startOffset = align(ctx.offset, 16u);
			ctx.offset       = startOffset + align(elemSize, 16u) * m_count;

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{
						.relativeOffset = startOffset,
						.elemSize       = elemSize,
						.stride         = align(elemSize, 16u),
						.count          = m_count,
					});
			}
		}

	private:
		std::string m_name;
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
		BuildLayoutMap(LayoutMap* layoutMap, BuildLayoutMapContext& ctx) const noexcept override
		{
			auto startOffset = align(ctx.offset, 16u);

			auto ctx1 = BuildLayoutMapContext{};
			m_structNode.BuildLayoutMap(layoutMap, ctx1);

			ctx.offset = startOffset + align(ctx1.offset, 16u) * m_count;

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{
						.relativeOffset = startOffset,
						.elemSize       = ctx1.offset,
						.stride         = align(ctx1.offset, 16u),
						.count          = m_count,
					});
			}
		}

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
		const DynamicConstantBufferDesc& elementDesc)
	{
		auto ctx = DynamicConstantBufferDesc::BuildLayoutMapContext{};
		elementDesc.root->BuildLayoutMap(&m_layoutMap, ctx);
		auto totalSize = align(ctx.offset, 16u);
		DynamicBuffer::Init(device, totalSize, elementDesc.updateFrequency, elementDesc.name);
	}

	DynamicConstantBuffer::View
	DynamicConstantBuffer::View::At(std::string_view key) const
	{
		if (IsNull())
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Cannot at using a null view" };

		if (key.find('.') != std::string_view::npos)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
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
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Cannot at using a null view" };

		auto& entry = m_parent->GetLayoutEntry(m_key);

		if (index >= entry.count)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBuffer::View::At",
				                "Index out of range for entry: " + m_key };
		}

		if (entry.count == 1)
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
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

		throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
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
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
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
		if (GetUpdateFrequency() == UpdateFrequency::kPerDraw)
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
	DynamicConstantBufferDesc::AddStructArray(std::string_view name, uint32_t count)
	{
		if (name.empty())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc::AddStructArray",
				                "Struct name cannot be empty" };
		}
		root->AddNode(name, std::make_unique<StructArrayNode>(name, count));
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
