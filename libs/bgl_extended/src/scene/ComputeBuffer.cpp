#include "scene/ComputeBuffer.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include <core/err/util.h>
#include <cstdint>
#include <utility>

namespace bgl
{
	void
	ComputeBuffer::Init(ComputeBufferDesc desc, ResourceManagerRef resourceManager)
	{
		core::ensure(desc.initialCount > 0, "ComputeBuffer must have a positive count");
		core::ensure(desc.elementSize > 0, "ComputeBuffer element size must be greater than zero");
		core::ensure(resourceManager != nullptr, "ResourceManager cannot be null");

		m_Desc = std::move(desc);

		m_Storage.Init(
			std::move(resourceManager),
			m_Desc.debugName,
			m_Desc.elementSize,
			m_Desc.initialCount,
			true);
	}

	void
	ComputeBuffer::Resize(uint32_t newCount)
	{
		core::ensure(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");

		m_Storage.Grow(newCount, false);
		m_Desc.initialCount = m_Storage.GetCapacity();
	}
}
