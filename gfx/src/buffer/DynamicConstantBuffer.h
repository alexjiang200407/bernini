#pragma once
#include "buffer/DynamicBuffer.h"
#include <core/str/str.h>

namespace gfx
{
	class DynamicConstantBufferVisitor;

	class DynamicConstantBufferDesc
	{
	private:
		struct BuildDynamicBufferContext
		{
			uint32_t offset    = 0;
			bool     alignNext = false;
		};

		class Node
		{
		public:
			virtual ~Node();

			virtual void
			AddNode(std::string_view path, std::unique_ptr<Node>&& toAdd) = 0;

			virtual void
			BuildDynamicBufferDesc(DynamicBufferDesc&, BuildDynamicBufferContext&)
				const noexcept = 0;
		};

		class StructNode;
		class ElementNode;
		class ElementArrayNode;
		class StructArrayNode;

	public:
		constexpr static uint32_t CONSTANT_BUFFER_ALIGNMENT = 16;

		DynamicConstantBufferDesc();

		DynamicConstantBufferDesc&
		AddStruct(std::string_view name);

		DynamicConstantBufferDesc&
		AddElementArray(std::string_view name, ElementType type, uint32_t count);

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
		std::unique_ptr<Node>              root;
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
