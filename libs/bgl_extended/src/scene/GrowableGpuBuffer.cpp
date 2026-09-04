#include "scene/GrowableGpuBuffer.h"
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include <bgl_common/gassert.h>

#include <algorithm>
#include <core/err/util.h>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace bgl
{
	namespace
	{
		// Past this, doubling costs more in transient old+new residency than it saves in growth
		// events, so growth tapers to x1.5.
		constexpr uint64_t c_TaperBytes  = 64ull * 1024 * 1024;
		constexpr uint32_t c_MinCapacity = 64;

		BufferHandle
		CreateStorage(
			const ResourceManagerRef& resourceManager,
			const std::string&        debugName,
			uint32_t                  stride,
			uint32_t                  capacity,
			bool                      isUav,
			bool                      isRaw)
		{
			if (isRaw)
			{
				RawViewDesc rawDesc;
				rawDesc.debugName = debugName;
				rawDesc.byteSize  = static_cast<uint64_t>(stride) * capacity;
				rawDesc.isUav     = isUav;

				return resourceManager->CreateRawBuffer(rawDesc);
			}

			StructBufferDesc desc;
			desc.debugName    = debugName;
			desc.elementCount = capacity;
			desc.stride       = stride;
			desc.isUav        = isUav;

			return resourceManager->CreateStructBuffer(desc);
		}
	}

	uint32_t
	NextGpuBufferCapacity(uint32_t current, uint32_t required, uint32_t stride) noexcept
	{
		// A buffer started at a handful of elements would otherwise grow 1 -> 2 -> 4, paying a full
		// forward copy each step.
		uint64_t next = std::max<uint64_t>(current, c_MinCapacity);

		while (next < required)
		{
			// Integer ratios, so the sequence is reproducible across platforms. The +1 keeps the
			// taper making progress at small sizes, where next/2 would round to nothing.
			next = next * stride >= c_TaperBytes ? next + next / 2 + 1 : next * 2;
		}

		return next <= std::numeric_limits<uint32_t>::max() ? static_cast<uint32_t>(next) :
		                                                      std::numeric_limits<uint32_t>::max();
	}

	void
	GrowableGpuBuffer::Init(
		ResourceManagerRef resourceManager,
		std::string        debugName,
		uint32_t           stride,
		uint32_t           capacity,
		bool               isUav,
		bool               isRaw)
	{
		gassert(resourceManager != nullptr, "GrowableGpuBuffer requires a valid ResourceManager");
		gassert(stride > 0, "GrowableGpuBuffer requires a positive stride");
		gassert(capacity > 0, "GrowableGpuBuffer requires a positive capacity");

		m_ResourceManager = std::move(resourceManager);
		m_DebugName       = std::move(debugName);
		m_Stride          = stride;
		m_IsUav           = isUav;
		m_IsRaw           = isRaw;

		m_Handle =
			CreateStorage(m_ResourceManager, m_DebugName, m_Stride, capacity, m_IsUav, m_IsRaw);
		if (m_Handle.IsNull())
		{
			core::throw_runtime_error(
				"Buffer '{}': could not allocate {} bytes of device memory for its initial {} "
				"elements",
				m_DebugName,
				static_cast<uint64_t>(capacity) * stride,
				capacity);
		}

		m_Capacity = capacity;
	}

	void
	GrowableGpuBuffer::Grow(uint32_t newCapacity, bool preserveContents)
	{
		gassert(IsInitialized(), "GrowableGpuBuffer is uninitialized; call Init() first");

		if (newCapacity <= m_Capacity)
			return;

		auto grown =
			CreateStorage(m_ResourceManager, m_DebugName, m_Stride, newCapacity, m_IsUav, m_IsRaw);
		if (grown.IsNull())
		{
			core::throw_runtime_error(
				"Buffer '{}': could not grow from {} to {} elements ({} bytes of device memory); "
				"the buffer is unchanged",
				m_DebugName,
				m_Capacity,
				newCapacity,
				static_cast<uint64_t>(newCapacity) * m_Stride);
		}

		// Only the first supersede since the last flush still holds data; the ones after it were
		// replaced before anything was copied in, so the copy source must not advance.
		if (m_Superseded.empty() && preserveContents)
		{
			m_CopyBytes = static_cast<uint64_t>(m_Capacity) * m_Stride;
		}

		m_Superseded.push_back(m_Handle);
		m_Handle   = grown;
		m_Capacity = newCapacity;
	}

	void
	GrowableGpuBuffer::FlushGrowth(ICommandList* cmdList)
	{
		if (m_Superseded.empty())
			return;

		gassert(cmdList != nullptr, "FlushGrowth requires a valid ICommandList");
		gassert(cmdList->IsOpen(), "ICommandList must be open to flush a buffer growth");

		if (m_CopyBytes > 0)
		{
			// The retiring resource is not a FrameGraph resource, so the state it was left in is
			// not known here; kCommon is the only accessBefore that covers every one of them.
			BufferBarrierDesc toCopySource;
			toCopySource.syncBefore   = BarrierSyncFlag::kAllCommands;
			toCopySource.accessBefore = BarrierAccessFlag::kCommon;
			toCopySource.syncAfter    = BarrierSyncFlag::kCopy;
			toCopySource.accessAfter  = BarrierAccessFlag::kCopySource;

			cmdList->Barrier(m_Superseded.front(), toCopySource);
			cmdList->CopyBuffer(m_Handle, m_Superseded.front(), 0, 0, m_CopyBytes);

			// The caller's dirty-region uploads write m_Handle right after this, as copy-dest, and
			// a freshly-added range below the old capacity overlaps the bytes just copied. Two
			// copy-dest writes to one buffer are unordered without a barrier between them, so the
			// upload could land before this copy and be overwritten by stale source bytes.
			BufferBarrierDesc orderWrites;
			orderWrites.syncBefore   = BarrierSyncFlag::kCopy;
			orderWrites.accessBefore = BarrierAccessFlag::kCopyDest;
			orderWrites.syncAfter    = BarrierSyncFlag::kCopy;
			orderWrites.accessAfter  = BarrierAccessFlag::kCopyDest;

			cmdList->Barrier(m_Handle, orderWrites);
		}

		for (BufferHandle superseded : m_Superseded)
		{
			m_ResourceManager->DestroyBuffer(superseded, true);
		}

		m_Superseded.clear();
		m_CopyBytes = 0;
	}

	void
	GrowableGpuBuffer::Release(bool deferred) noexcept
	{
		if (m_ResourceManager == nullptr)
			return;

		for (BufferHandle superseded : m_Superseded)
		{
			m_ResourceManager->DestroyBuffer(superseded, deferred);
		}
		m_Superseded.clear();
		m_CopyBytes = 0;

		if (!m_Handle.IsNull())
		{
			m_ResourceManager->DestroyBuffer(m_Handle, deferred);
			m_Handle = {};
		}

		m_Capacity = 0;
		m_ResourceManager.Reset();
	}
}
