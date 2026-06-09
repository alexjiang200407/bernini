#pragma once
#include "types/Barrier.h"

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

	struct BufferDesc
	{
		enum class CpuAccessMode
		{
			kDefault,
			kUpload,
			kReadBack,
		} cpuAccess = CpuAccessMode::kDefault;

		uint32_t    stride       = 0;
		uint32_t    elementCount = 0;
		std::string debugName    = "Unnamed Buffer";
		bool        isUav        = false;
	};

}
