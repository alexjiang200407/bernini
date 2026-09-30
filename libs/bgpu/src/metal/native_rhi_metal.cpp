#include "cmd/CommandQueue_metal.h"
#include "convert_metal.h"
#include "resource/Texture_metal.h"
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/metal/native_rhi.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Format.h>

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

	MTL::PixelFormat
	ToMtlPixelFormat(Format format) noexcept
	{
		return ConvertFormat(format);
	}
}
