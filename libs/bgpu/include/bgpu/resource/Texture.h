#pragma once
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ClearValue.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/TextureDimension.h>
#include <core/containers/enum_set.h>
#include <core/containers/slot_handle.h>
#include <cstdint>
#include <string>
#include <utility>

namespace bgpu
{
	class Texture;

	struct TextureBarrierDesc
	{
		BarrierSync syncBefore = BarrierSyncFlag::kNone;
		BarrierSync syncAfter  = BarrierSyncFlag::kNone;

		BarrierAccess accessBefore = BarrierAccessFlag::kNone;
		BarrierAccess accessAfter  = BarrierAccessFlag::kNone;

		BarrierLayout layoutBefore = BarrierLayout::kUndefined;
		BarrierLayout layoutAfter  = BarrierLayout::kUndefined;

		uint32_t baseMipLevel   = 0;
		uint32_t mipCount       = uint32_t(-1);
		uint32_t baseArrayLayer = 0;
		uint32_t layerCount     = uint32_t(-1);
		uint32_t planeCount     = 1;
		uint32_t firstPlane     = 0;

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

		template <typename Self>
		Self&&
		SetLayoutBefore(this Self&& self, BarrierLayout layout)
		{
			self.layoutBefore = layout;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetLayoutAfter(this Self&& self, BarrierLayout layout)
		{
			self.layoutAfter = layout;
			return std::forward<Self>(self);
		}
	};

	enum class TextureUsageFlag : uint32_t
	{
		kSRV          = 0x00000001,
		kDepthStencil = 0x00000010,
		kRenderTarget = 0x00000100,
	};

	using TextureUsage = core::enum_set<TextureUsageFlag>;

	struct TextureDesc
	{
		uint32_t width         = 1;
		uint32_t height        = 1;
		uint32_t depth         = 1;
		uint32_t arraySize     = 1;
		uint32_t mipLevels     = 1;
		uint32_t sampleCount   = 1;
		uint32_t sampleQuality = 0;

		TextureUsage usage = TextureUsageFlag::kSRV;

		Format           format    = Format::UNKNOWN;
		TextureDimension dimension = TextureDimension::kTexture2D;
		ClearValue       clearValue;
		std::string      debugName;

		BarrierLayout initialLayout = BarrierLayout::kCommon;
	};

	/** A texture resource on the GPU, owned by the resource manager that made it. */
	struct TextureHandle
	{
		core::slot_handle slot;

		[[nodiscard]] bool
		IsNull() const
		{
			return slot.is_null();
		}

		[[nodiscard]] bool
		operator==(const TextureHandle& other) const noexcept
		{
			return slot == other.slot;
		}

		[[nodiscard]] bool
		operator!=(const TextureHandle& other) const noexcept
		{
			return !(*this == other);
		}
	};

	struct TextureSubresourceData
	{
		const void* data       = nullptr;
		uint64_t    rowPitch   = 0;  // bytes between rows of `data`
		uint64_t    slicePitch = 0;  // bytes between 2D slices of `data`
	};
}
