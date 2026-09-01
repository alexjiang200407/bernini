#include "scene/ComputeBuffer.h"

namespace bgl
{
	void
	ComputeBuffer::Init(ComputeBufferDesc desc, ResourceManagerRef resourceManager)
	{
		gassert(desc.initialCount > 0, "ComputeBuffer must have a positive count");
		gassert(desc.elementSize > 0, "ComputeBuffer element size must be greater than zero");
		gassert(resourceManager != nullptr, "ResourceManager cannot be null");

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
		gassert(IsInitialized(), "ComputeBuffer is uninitialized; call Init() first");

		m_Storage.Grow(newCount, false);
		m_Desc.initialCount = m_Storage.GetCapacity();
	}
}
