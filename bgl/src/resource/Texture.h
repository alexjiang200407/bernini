#pragma once
#include "types/Barrier.h"
#include "types/ClearValue.h"
#include "types/Format.h"
#include "types/TextureDimension.h"
#include <core/containers/enum_set.h>

namespace bgl
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

		TextureBarrierDesc&
		AddSyncBefore(BarrierSyncFlag sync)
		{
			syncBefore |= sync;
			return *this;
		}

		TextureBarrierDesc&
		AddSyncAfter(BarrierSyncFlag sync)
		{
			syncAfter |= sync;
			return *this;
		}

		TextureBarrierDesc&
		AddAccessBefore(BarrierAccessFlag access)
		{
			accessBefore |= access;
			return *this;
		}

		TextureBarrierDesc&
		AddAccessAfter(BarrierAccessFlag access)
		{
			accessAfter |= access;
			return *this;
		}

		TextureBarrierDesc&
		SetLayoutBefore(BarrierLayout layout)
		{
			layoutBefore = layout;
			return *this;
		}

		TextureBarrierDesc&
		SetLayoutAfter(BarrierLayout layout)
		{
			layoutAfter = layout;
			return *this;
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

		BarrierLayout initalLayout = BarrierLayout::kCommon;
	};

	struct TextureHandle
	{
		uint32_t     idx        = 0xFFFFFFFF;
		uint32_t     generation = 0;
		TextureUsage usage      = TextureUsageFlag::kSRV;

		[[nodiscard]] bool
		IsNull() const
		{
			return idx == 0xFFFFFFFF;
		}
	};
}
