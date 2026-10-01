#include "gfx/ExternalBuffer.h"
#include <utility>

namespace bgl
{
	ExternalBuffer::ExternalBuffer(
		bgpu::ResourceManagerRef resourceManager,
		bgpu::BufferHandle       handle) noexcept :
		m_ResourceManager(std::move(resourceManager)), m_Handle(handle)
	{}

	ExternalBuffer::~ExternalBuffer() noexcept { m_ResourceManager->DestroyBuffer(m_Handle); }
}
