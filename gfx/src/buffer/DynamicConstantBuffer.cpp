#include "buffer/DynamicConstantBuffer.h"
#include "math/util.h"

namespace gfx
{
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

	DynamicConstantBufferDesc&
	DynamicConstantBufferDesc::AddStruct(std::string_view path)
	{
		struct AddStructVisitor : public Node::PathVisitor
		{
			std::string_view structPath;

			void
			VisitLeaf(Node& node) override
			{
				if (node.AsElement())
				{
					std::string description = std::format("'{}' already exists", node.m_path);
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc exception",
						                description };
				}
			}

			void
			VisitStruct(Node& node, Node::StructChildren&) override
			{
				auto path = std::string_view(node.m_path);
				if (!path.empty() && path.substr(1) == structPath)
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc exception",
						                std::format("'{}' already exists", node.m_path) };
				}
			}

			void
			VisitStructInvalid(
				Node&                 node,
				Node::StructChildren& structChildren,
				std::string_view      invalid,
				std::string_view      next) override
			{
				if (!next.empty())
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc exception",
						                "Element doesn't exists: "s + std::string(invalid) };
				}
				auto path = std::format("{}.{}", node.m_path, invalid);
				structChildren.Emplace(invalid, path, Node::StructChildren{});
			}
		} visitor;

		visitor.structPath = path;

		root.Walk(path, visitor);

		return *this;
	}

	DynamicConstantBufferDesc&
	DynamicConstantBufferDesc::AddElement(std::string_view path, ElementType format)
	{
		if (path.empty())
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicConstantBufferDesc exception",
				                "Element name cannot be empty" };

		struct AddElementVisitor : public Node::PathVisitor
		{
			ElementType format = ElementType::kInvalid;

			void
			VisitLeaf(Node& node) override
			{
				if (node.AsElement())
				{
					std::string description = std::format("'{}' already exists", node.m_path);
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc exception",
						                description };
				}
			}

			void
			VisitStructInvalid(
				Node&                 node,
				Node::StructChildren& structChildren,
				std::string_view      invalid,
				std::string_view      next) override
			{
				if (!next.empty())
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBufferDesc exception",
						                "Element doesn't exists: "s + std::string(invalid) };
				}
				auto path = std::format("{}.{}", node.m_path, invalid);
				structChildren.Emplace(invalid, path, Node::Element{ invalid, format });
			}
		} visitor;

		visitor.format = format;
		root.Walk(path, visitor);

		return *this;
	}

	void
	DynamicConstantBufferDesc::Node::Walk(std::string_view path, PathVisitor& vis)
	{
		auto [head, tail] = core::str::splitOnce(path, ".");

		if (tail.empty())
		{
			vis.VisitLeaf(*this);
		}

		if (auto* structChildren = AsStruct())
		{
			vis.VisitStruct(*this, *structChildren);

			if (head.empty())
				return;

			auto* childNode = structChildren->Find(head);

			if (!childNode)
				vis.VisitStructInvalid(*this, *structChildren, head, tail);
			else
			{
				childNode->Walk(tail, vis);
			}
		}
		else if (AsArray())
		{
			// TODO:
			//vis.VisitArray(AsArray());
		}
	}

	void
	DynamicConstantBufferDesc::Node::Dfs(Visitor& vis) const
	{
		if (AsElement())
		{
			// All elements are leaves
			vis.VisitLeaf(*this);
		}
		else if (auto* structChildren = AsStruct())
		{
			if (structChildren->Empty())
			{
				vis.VisitLeaf(*this);
				return;
			}

			vis.VisitStruct(*this, *structChildren);
			for (auto& children : *structChildren)
			{
				children.Dfs(vis);
			}
		}
		else if (AsArray())
		{
			// TODO:
			//vis.VisitArray(AsArray());
		}
	}

	DynamicBufferDesc
	DynamicConstantBufferDesc::ToDynamicBufferDesc() const
	{
		auto desc = DynamicBufferDesc{};
		desc.SetName(name).SetAlignment(16);

		struct ToDynamicBufferDescVisitor : public Node::Visitor
		{
			uint32_t totalOffset     = 0u;
			bool     shouldAlignNext = false;

			ToDynamicBufferDescVisitor(DynamicBufferDesc& desc) : desc{ desc } {}
			DynamicBufferDesc& desc;

			void
			VisitLeaf(const Node& node) override
			{
				auto path = std::string_view(node.m_path).substr(1);
				if (auto elem = node.AsElement())
				{
					const uint32_t elemSz = sizeOfElementType(elem->GetType());

					uint32_t padding = 0;

					{
						const uint32_t offsetInDword  = totalOffset & 3u;
						const uint32_t remainingDword = 4u - offsetInDword;

						if (elemSz > remainingDword)
						{
							const uint32_t alignedOffset = align(totalOffset, 4u);
							padding += alignedOffset - totalOffset;
							totalOffset = alignedOffset;
						}
					}

					{
						const uint32_t offsetInReg = totalOffset & 15u;
						const uint32_t remaining   = 16u - offsetInReg;

						if (elemSz > remaining || shouldAlignNext)
						{
							const uint32_t alignedOffset = align(totalOffset, 16u);
							padding += alignedOffset - totalOffset;
							totalOffset     = alignedOffset;
							shouldAlignNext = false;
						}
					}

					desc.AddElement(path, elem->GetType(), padding);
					totalOffset += elemSz;
				}
			}

			void
			VisitStruct(const Node& node, const Node::StructChildren& structChildren) override
			{
				if (!structChildren.Empty())
				{
					shouldAlignNext = true;
				}
			}

		} visitor{ desc };

		root.Dfs(visitor);

		return desc;
	}
}
