#include "scene/ComputeBuffer.h"

namespace bgl
{
	void
	ComputeBuffer::Init(ComputeBufferDesc desc, ResourceManagerRef resourceManager) noexcept
	{
		gassert(desc.maxCount > 0, "ComputeBuffer must have a positive count");
		gassert(desc.elementSize > 0, "ComputeBuffer element size must be greater than zero");
		gassert(resourceManager != nullptr, "ResourceManager cannot be null");

		m_Desc            = std::move(desc);
		m_ResourceManager = std::move(resourceManager);
		m_Handle          = m_ResourceManager->CreateComputeBuffer(m_Desc);
	}
}
