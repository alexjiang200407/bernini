#include "cmd/CommandQueue_metal.h"
#include "resource/Texture_metal.h"
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/metal/native_rhi.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>

namespace bgpu
{
	MTL::Texture*
	GetMtlTexture(const IResourceManager& resourceManager, TextureHandle texture) noexcept
	{
		return resourceManager.GetTexture(texture).GetMTLResource();
	}

	MTL::CommandBuffer*
	NewMtlCommandBuffer(ICommandQueue& queue) noexcept
	{
		return static_cast<const CommandQueue&>(queue).NewCommandBuffer();
	}
}
