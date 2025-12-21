#pragma once
#include "buffer/DynamicBuffer.h"
#include <core/OrderedMap.h>
#include <core/str/str.h>

namespace gfx
{
	class DynamicConstantBufferVisitor;

	class DynamicConstantBufferDesc
	{
	private:
		struct Node;
		using NodesList =
			core::OrderedMap<std::string, Node, core::str::StringViewHash, core::str::StringViewEq>;

		class Node
		{
		private:
			using StructChildren = NodesList;
			using ArrayChildren  = std::vector<Node>;

			class Element
			{
			public:
				Element(std::string_view name, ElementType type) : name(name), type(type) {}

				std::string_view
				GetName() const noexcept
				{
					return name;
				}
				ElementType
				GetType() const noexcept
				{
					return type;
				}

			private:
				std::string name;
				ElementType type;
			};

			using Storage = std::variant<StructChildren, ArrayChildren, Element>;

			struct Visitor
			{
				// Nodes with no children
				virtual void
				VisitLeaf(Node& leaf)
				{
					(void)leaf;
				}

				virtual void
				VisitLeaf(const Node& leaf)
				{
					(void)leaf;
				}

				virtual void
				VisitStruct(const Node& node, const StructChildren& structChildren)
				{
					(void)node;
					(void)structChildren;
				}

				virtual void
				VisitStruct(Node& node, StructChildren& structChildren)
				{
					(void)node;
					(void)structChildren;
				}
			};

			// For visiting a given path e.g. light.pos.x
			struct PathVisitor : public Visitor
			{
				virtual void
				VisitArrayInvalid(
					Node&            parent,
					ArrayChildren&   arrayChildren,
					std::string_view invalid,
					std::string_view next)
				{
					(void)arrayChildren;
					(void)parent;
					(void)invalid;
					(void)next;
				}

				virtual void
				VisitStructInvalid(
					Node&            parent,
					StructChildren&  structChildren,
					std::string_view invalid,
					std::string_view next)
				{
					(void)structChildren;
					(void)parent;
					(void)invalid;
					(void)next;
				}
			};

		private:
			explicit Node(StructChildren children) : storage(std::move(children)), m_path{ "" } {}

		public:
			explicit Node(std::string_view path, StructChildren children) :
				storage(std::move(children)), m_path{ path }
			{}

			explicit Node(std::string_view path, ArrayChildren children) :
				storage(std::move(children)), m_path{ path }
			{}

			explicit Node(std::string_view path, Element leaf) :
				storage(std::move(leaf)), m_path{ path }
			{}

			StructChildren*
			AsStruct()
			{
				if (!std::holds_alternative<StructChildren>(storage))
					return nullptr;

				return std::addressof(std::get<StructChildren>(storage));
			}

			StructChildren const*
			AsStruct() const
			{
				if (!std::holds_alternative<StructChildren>(storage))
					return nullptr;

				return std::addressof(std::get<StructChildren>(storage));
			}

			ArrayChildren*
			AsArray()
			{
				if (!std::holds_alternative<ArrayChildren>(storage))
					return nullptr;
				return std::addressof(std::get<ArrayChildren>(storage));
			}

			const ArrayChildren*
			AsArray() const
			{
				if (!std::holds_alternative<ArrayChildren>(storage))
					return nullptr;
				return std::addressof(std::get<ArrayChildren>(storage));
			}

			Element*
			AsElement()
			{
				if (!std::holds_alternative<Element>(storage))
					return nullptr;
				return std::addressof(std::get<Element>(storage));
			}

			const Element*
			AsElement() const
			{
				if (!std::holds_alternative<Element>(storage))
					return nullptr;
				return std::addressof(std::get<Element>(storage));
			}

			[[nodiscard]]
			bool
			IsRoot() const noexcept
			{
				return m_path.empty();
			}

			void
			Walk(std::string_view path, PathVisitor& vis);

			void
			Dfs(Visitor& vis) const;

		private:
			Storage     storage;
			std::string m_path;

			friend class DynamicConstantBufferDesc;
		};

	public:
		constexpr static uint32_t CONSTANT_BUFFER_ALIGNMENT = 16;

		DynamicConstantBufferDesc&
		AddStruct(std::string_view name);

		DynamicConstantBufferDesc&
		AddElement(std::string_view name, ElementType format);

		DynamicConstantBufferDesc&
		SetName(std::string_view name)
		{
			this->name = name;
			return *this;
		}

		DynamicConstantBufferDesc&
		SetUpdateFrequency(DynamicBufferDesc::UpdateFrequency updateFrequency)
		{
			this->updateFrequency = updateFrequency;
			return *this;
		}

		DynamicBufferDesc
		ToDynamicBufferDesc() const;

	private:
		Node                               root{ Node::StructChildren{} };
		std::string                        name;
		DynamicBufferDesc::UpdateFrequency updateFrequency =
			DynamicBufferDesc::UpdateFrequency::kPerFrame;

		friend class DynamicConstantBuffer;
	};

	class DynamicConstantBuffer : public DynamicBuffer
	{
	public:
		DynamicConstantBuffer() noexcept = default;
		DynamicConstantBuffer(
			nvrhi::DeviceHandle              device,
			const DynamicConstantBufferDesc& elementDesc);

		DynamicBufferItem::View
		operator[](std::string_view name) noexcept;

		DynamicBufferItem::View
		At(std::string_view name);

		nvrhi::BindingLayoutItem
		GetBindingLayoutItem(uint32_t slot) const noexcept;

		nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const noexcept;

	private:
		using DynamicBuffer::operator[];
	};
}
