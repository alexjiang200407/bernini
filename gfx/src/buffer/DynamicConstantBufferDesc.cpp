#include "buffer/DynamicConstantBufferDesc.h"
#include "math/util.h"
#include <core/OrderedMap.h>

namespace
{
	constexpr uint32_t
	align16(uint32_t value)
	{
		return gfx::align(value, 16u);
	}
}

namespace gfx
{
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
			ctx.offset           = align16(ctx.offset);
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
					LayoutEntry{ .relativeOffset = prevOffset - ctx.parentOffset,
				                 .elemSize       = elemSize,
				                 .stride         = 0,
				                 .count          = 1,
				                 .groupType      = GroupType::Struct,
				                 .elemType       = ElementType::kInvalid });
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
				const uint32_t alignedOffset = align16(ctx.offset);
				padding                      = alignedOffset - ctx.offset;
				ctx.offset                   = alignedOffset;
			}

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{ .relativeOffset = ctx.offset - ctx.parentOffset,
				                 .elemSize       = elemSize,
				                 .stride         = 0,
				                 .count          = 1,
				                 .groupType      = GroupType::Single,
				                 .elemType       = m_type });
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
			auto startOffset = align16(ctx.offset);
			ctx.offset       = startOffset + align16(elemSize) * (m_count - 1) + elemSize;

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{ .relativeOffset = startOffset,
				                 .elemSize       = elemSize,
				                 .stride         = align16(elemSize),
				                 .count          = m_count,
				                 .groupType      = GroupType::Array,
				                 .elemType       = m_type });
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
			auto startOffset = align16(ctx.offset);

			auto ctx1 = BuildLayoutMapContext{};
			m_structNode.BuildLayoutMap(layoutMap, ctx1);

			ctx.offset = startOffset + align16(ctx1.offset) * (m_count - 1) + ctx1.offset;

			if (layoutMap)
			{
				layoutMap->emplace(
					m_name,
					LayoutEntry{ .relativeOffset = startOffset,
				                 .elemSize       = ctx1.offset,
				                 .stride         = align16(ctx1.offset),
				                 .count          = m_count,
				                 .groupType{ GroupType::Array, GroupType::Struct },
				                 .elemType = ElementType::kInvalid });
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

	DynamicConstantBufferDesc::Node::~Node() {}
}
