#pragma once
#include "types/Barrier.h"
#include <core/type_traits.h>

namespace bgl
{
	class Buffer;
	class ResourceManager;

	struct BufferHandle
	{
		uint32_t idx        = 0xFFFFFFFF;
		uint32_t generation = 0;

		[[nodiscard]] bool
		IsNull() const
		{
			return idx == 0xFFFFFFFF;
		}
	};

	struct BufferBarrierDesc
	{
		BarrierSync   syncBefore   = BarrierSyncFlag::kNone;
		BarrierSync   syncAfter    = BarrierSyncFlag::kNone;
		BarrierAccess accessBefore = BarrierAccessFlag::kNone;
		BarrierAccess accessAfter  = BarrierAccessFlag::kNone;

		BufferBarrierDesc&
		AddSyncBefore(BarrierSyncFlag sync)
		{
			syncBefore |= sync;
			return *this;
		}

		BufferBarrierDesc&
		AddSyncAfter(BarrierSyncFlag sync)
		{
			syncAfter |= sync;
			return *this;
		}

		BufferBarrierDesc&
		AddAccessBefore(BarrierAccessFlag access)
		{
			accessBefore |= access;
			return *this;
		}

		BufferBarrierDesc&
		AddAccessAfter(BarrierAccessFlag access)
		{
			accessAfter |= access;
			return *this;
		}
	};

	struct StructBufferDesc
	{
		uint32_t    stride       = 0;
		uint32_t    elementCount = 0;
		std::string debugName    = "Unnamed Buffer";
		bool        isUav        = false;

		template <core::type_traits::trivially_copyable T>
		StructBufferDesc&
		SetElement() noexcept
		{
			stride = sizeof(T);
			return *this;
		}

		StructBufferDesc&
		SetElementCount(uint32_t count) noexcept
		{
			elementCount = count;
			return *this;
		}

		StructBufferDesc&
		SetIsUav(bool isUav_ = true) noexcept
		{
			isUav = isUav_;
			return *this;
		}

		StructBufferDesc&
		SetDebugName(std::string debugName_) noexcept
		{
			debugName = std::move(debugName_);
			return *this;
		}
	};

	struct ConstantBufferDesc
	{
		uint32_t    size      = 0;
		std::string debugName = "Unnamed Constant Buffer";

		template <core::type_traits::trivially_copyable T>
		ConstantBufferDesc&
		SetElement() noexcept
		{
			size = sizeof(T);
			return *this;
		}

		ConstantBufferDesc&
		SetDebugName(std::string debugName_) noexcept
		{
			debugName = std::move(debugName_);
			return *this;
		}
	};

	struct ComputeBufferDesc
	{
		uint32_t    maxCount    = 0;
		uint32_t    elementSize = 0;
		std::string debugName   = "Unnamed Compute Buffer";

		template <core::type_traits::trivially_copyable T>
		ComputeBufferDesc&
		SetElement() noexcept
		{
			elementSize = sizeof(T);
			return *this;
		}

		ComputeBufferDesc&
		SetMaxCount(uint32_t count) noexcept
		{
			maxCount = count;
			return *this;
		}

		ComputeBufferDesc&
		SetDebugName(std::string debugName_) noexcept
		{
			debugName = std::move(debugName_);
			return *this;
		}
	};

}
