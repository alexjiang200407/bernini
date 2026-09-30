#pragma once
#include <Metal/Metal.hpp>
#include <bgpu/api.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Format.h>

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

	/** The pixel format a texture of `format` is created with. */
	[[nodiscard]] BGPU_API MTL::PixelFormat
						   ToMtlPixelFormat(Format format) noexcept;
}
