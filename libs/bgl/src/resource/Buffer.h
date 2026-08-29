#pragma once
#include "types/Barrier.h"
#include <core/containers/slot_handle.h>
#include <core/type_traits.h>

namespace bgl
{
	class Buffer;
	class ResourceManager;

	struct BufferHandle
	{
		core::slot_handle slot;

		// What a shader must find in a constant buffer to reach this resource. The backend that
		// created the handle decides it: a descriptor-heap index on D3D12, the pool slot Metal's
		// dispatch rewrite looks the resource up by. Null until a resource manager hands one out.
		uint32_t bindlessIndex = core::slot_handle::invalid_index;

		[[nodiscard]] bool
		IsNull() const
		{
			return slot.index == 0xFFFFFFFF;
		}
	};

	// What a backend allocated a buffer as. Every Create*Buffer lowers its own descriptor to this
	// one, and it is what GetBufferDesc hands back to code that holds only a handle.
	struct BufferDesc
	{
		uint64_t byteSize = 0;
		bool     isUav    = false;

		// The one view the buffer has: a shader reads a raw buffer as a ByteAddressBuffer and a
		// structured one as a StructuredBuffer<T>, and the wrong wrapper on either is undefined.
		bool        isRaw     = false;
		std::string debugName = "Unnamed Buffer";
	};

	// A raw view addresses bytes with a uint, so one buffer cannot reach past this however large the
	// resource behind it is. A mirror buffer refuses to grow past it rather than wrap.
	constexpr uint64_t c_MaxRawBufferBytes = uint64_t(1) << 32;

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

	// A buffer of bytes rather than of elements: the shader reads it as a ByteAddressBuffer and
	// decides the type at each load, which is what a payload whose layout varies per record needs.
	struct RawBufferDesc
	{
		uint64_t    byteSize  = 0;
		std::string debugName = "Unnamed Raw Buffer";
		bool        isUav     = false;

		RawBufferDesc&
		SetByteSize(uint64_t byteSize_) noexcept
		{
			byteSize = byteSize_;
			return *this;
		}

		RawBufferDesc&
		SetIsUav(bool isUav_ = true) noexcept
		{
			isUav = isUav_;
			return *this;
		}

		RawBufferDesc&
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
		uint32_t    initialCount = 0;
		uint32_t    elementSize  = 0;
		std::string debugName    = "Unnamed Compute Buffer";

		template <core::type_traits::trivially_copyable T>
		ComputeBufferDesc&
		SetElement() noexcept
		{
			elementSize = sizeof(T);
			return *this;
		}

		ComputeBufferDesc&
		SetInitialCount(uint32_t count) noexcept
		{
			initialCount = count;
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
