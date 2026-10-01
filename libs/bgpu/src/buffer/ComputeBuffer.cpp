#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/err/util.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	ComputeBuffer::ComputeBuffer(ResourceManagerRef resourceManager, ComputeBufferDesc desc) :
		m_Desc(std::move(desc)), m_Storage(
									 std::move(resourceManager),
									 m_Desc.debugName,
									 m_Desc.elementSize,
									 m_Desc.initialCount,
									 true)
	{}

	void
	ComputeBuffer::Resize(uint32_t newCount)
	{
		m_Storage.Grow(newCount, false);
		m_Desc.initialCount = m_Storage.GetCapacity();
	}
}
