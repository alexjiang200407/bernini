#pragma once
#include <bgpu/types/Barrier.h>
#include <core/containers/slot_handle.h>
#include <core/type_traits.h>
#include <cstdint>
#include <string>
#include <utility>

namespace bgpu
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

		// The view the buffer was created with: a shader reads a raw buffer as a ByteAddressBuffer
		// and a structured one as a StructuredBuffer<T>, and the wrong wrapper on either is
		// undefined. A second, structured view may be added with CreateBufferSrv.
		bool        isRaw     = false;
		std::string debugName = "Unnamed Buffer";
	};

	// A raw view addresses bytes with a uint, so one buffer cannot reach past this however large the
	// resource behind it is. A mirror buffer refuses to grow past it rather than wrap.
	constexpr uint64_t c_MaxRawBufferBytes = uint64_t(1) << 32;

	// A second, structured view of a buffer that already has one, and what a shader binds to reach
	// it. Separate from BufferHandle because a view is not the resource: destroying the buffer does
	// not destroy this, exactly as with an Srv onto a texture.
	struct BufferSrvHandle
	{
		core::slot_handle slot;
		uint32_t          bindlessIndex = core::slot_handle::invalid_index;

		[[nodiscard]] bool
		IsNull() const
		{
			return slot.index == core::slot_handle::invalid_index;
		}
	};

	// A raw arena and the typed view of the same allocation, bound as a unit. Separate members can
	// be handed different buffers; this cannot, which is the whole of ADR-9's shader half.
	struct RawArenaBinding
	{
		BufferHandle    buffer;
		BufferSrvHandle handles;
	};

	struct BufferSrvDesc
	{
		// Element size of the view, not of the buffer: the same bytes are read as elements of this.
		uint32_t    stride    = 0;
		std::string debugName = "Unnamed Buffer View";

		template <core::type_traits::trivially_copyable T, typename Self>
		Self&&
		SetElement(this Self&& self) noexcept
		{
			self.stride = sizeof(T);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}
	};

	struct BufferBarrierDesc
	{
		BarrierSync   syncBefore   = BarrierSyncFlag::kNone;
		BarrierSync   syncAfter    = BarrierSyncFlag::kNone;
		BarrierAccess accessBefore = BarrierAccessFlag::kNone;
		BarrierAccess accessAfter  = BarrierAccessFlag::kNone;

		template <typename Self>
		Self&&
		AddSyncBefore(this Self&& self, BarrierSyncFlag sync)
		{
			self.syncBefore |= sync;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddSyncAfter(this Self&& self, BarrierSyncFlag sync)
		{
			self.syncAfter |= sync;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddAccessBefore(this Self&& self, BarrierAccessFlag access)
		{
			self.accessBefore |= access;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddAccessAfter(this Self&& self, BarrierAccessFlag access)
		{
			self.accessAfter |= access;
			return std::forward<Self>(self);
		}
	};

	struct StructBufferDesc
	{
		uint32_t    stride       = 0;
		uint32_t    elementCount = 0;
		std::string debugName    = "Unnamed Buffer";
		bool        isUav        = false;

		template <core::type_traits::trivially_copyable T, typename Self>
		Self&&
		SetElement(this Self&& self) noexcept
		{
			self.stride = sizeof(T);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetElementCount(this Self&& self, uint32_t count) noexcept
		{
			self.elementCount = count;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetIsUav(this Self&& self, bool value = true) noexcept
		{
			self.isUav = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}
	};

	// A buffer of bytes rather than of elements: the shader reads it as a ByteAddressBuffer and
	// decides the type at each load, which is what a payload whose layout varies per record needs.
	//
	// Named for the view rather than the buffer, unlike its siblings, because RawBuffer is the
	// CPU-mirrored arena over one (<bgpu/buffer/RawBuffer.h>) and the Slang wrapper that reads it.
	struct RawViewDesc
	{
		uint64_t    byteSize  = 0;
		std::string debugName = "Unnamed Raw Buffer";
		bool        isUav     = false;

		template <typename Self>
		Self&&
		SetByteSize(this Self&& self, uint64_t value) noexcept
		{
			self.byteSize = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetIsUav(this Self&& self, bool value = true) noexcept
		{
			self.isUav = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}
	};

	struct ConstantBufferDesc
	{
		uint32_t    size      = 0;
		std::string debugName = "Unnamed Constant Buffer";

		template <core::type_traits::trivially_copyable T, typename Self>
		Self&&
		SetElement(this Self&& self) noexcept
		{
			self.size = sizeof(T);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}
	};

	struct ComputeBufferDesc
	{
		uint32_t    initialCount = 0;
		uint32_t    elementSize  = 0;
		std::string debugName    = "Unnamed Compute Buffer";

		template <core::type_traits::trivially_copyable T, typename Self>
		Self&&
		SetElement(this Self&& self) noexcept
		{
			self.elementSize = sizeof(T);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetInitialCount(this Self&& self, uint32_t count) noexcept
		{
			self.initialCount = count;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}
	};

}
