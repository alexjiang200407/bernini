#pragma once
#include <bgpu/api.h>
#include <bgpu/resource/Texture.h>

// Declared, not defined: a caller that dereferences one includes <Metal/Metal.hpp> itself.
namespace MTL
{
	class CommandBuffer;
	class Texture;
}

namespace bgpu
{
	class ICommandQueue;
	class IResourceManager;

	/**
	 * The Metal texture behind `texture`. Borrowed: the resource manager holds it, and destroying
	 * the texture there ends the pointer's life.
	 *
	 * @pre `texture` is a valid handle of `resourceManager`.
	 */
	[[nodiscard]] BGPU_API MTL::Texture*
	GetMtlTexture(const IResourceManager& resourceManager, TextureHandle texture) noexcept;

	/**
	 * A command buffer on `queue`, ordered after everything the queue has already been handed.
	 * Autoreleased: the caller scopes the pool it lands in.
	 */
	[[nodiscard]] BGPU_API MTL::CommandBuffer*
						   NewMtlCommandBuffer(ICommandQueue& queue) noexcept;
}
