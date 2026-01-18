#pragma once
#include "buffer/ElementType.h"
#include <core/str/str.h>

namespace gfx
{
	struct LayoutEntry
	{
		uint32_t relativeOffset;
		uint32_t elemSize;
		uint32_t stride;  // 0 if not array
		uint32_t count;   // 1 if not array

		[[nodiscard]] bool
		IsArray() const noexcept
		{
			return stride != 0;
		}
	};

	using LayoutMap = std::
		unordered_map<std::string, LayoutEntry, core::str::StringViewHash, core::str::StringViewEq>;

	class DynamicConstantBufferDesc
	{
	private:
		struct BuildLayoutMapContext
		{
			uint32_t offset       = 0;
			uint32_t parentOffset = 0;
		};

		class Node
		{
		public:
			virtual ~Node();

			virtual void
			AddNode(std::string_view path, std::unique_ptr<Node>&& toAdd) = 0;

			virtual void
			BuildLayoutMap(LayoutMap*, BuildLayoutMapContext&) const noexcept = 0;
		};

		class StructNode;
		class ElementNode;
		class ElementArrayNode;
		class StructArrayNode;

	public:
		DynamicConstantBufferDesc();

		DynamicConstantBufferDesc&
		AddStruct(std::string_view name);

		DynamicConstantBufferDesc&
		AddElementArray(std::string_view name, ElementType type, uint32_t count);

		DynamicConstantBufferDesc&
		AddStructArray(std::string_view name, uint32_t count);

		DynamicConstantBufferDesc&
		AddElement(std::string_view name, ElementType format);

		DynamicConstantBufferDesc&
		SetName(std::string_view name)
		{
			this->name = name;
			return *this;
		}

		std::string_view
		GetName() const noexcept
		{
			return name;
		}

		DynamicConstantBufferDesc&
		SetIsVolatile(bool isVolatile)
		{
			this->isVolatile = isVolatile;
			return *this;
		}

	private:
		std::unique_ptr<Node> root;
		std::string           name;
		bool                  isVolatile = false;

		friend class DynamicConstantBuffer;
	};
}
