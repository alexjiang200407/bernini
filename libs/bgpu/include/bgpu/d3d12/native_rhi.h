#pragma once

// Windows only, and empty elsewhere, as <bgpu/d3d12/native_device.h> is.
#if defined(_WIN32)

#	include <bgpu/api.h>
#	include <bgpu/resource/Texture.h>
#	include <directx/d3d12.h>

namespace bgpu
{
	class ICommandQueue;
	class IResourceManager;

	/**
	 * The D3D12 queue behind `queue`. Borrowed: `queue` holds the reference, and an owner that needs
	 * it past that object's life -- a swapchain presenting on it does -- adds its own.
	 */
	[[nodiscard]] BGPU_API ID3D12CommandQueue*
	GetD3d12CommandQueue(const ICommandQueue& queue) noexcept;

	/**
	 * Adopts a resource made outside the resource manager -- a swapchain's backbuffer -- as one of
	 * its textures, so it is viewed, barriered and destroyed like any other. The manager adds its
	 * own reference; destroying the texture releases only that.
	 *
	 * @pre `desc` describes `resource`, and `desc.initialLayout` is the layout it is in now.
	 */
	[[nodiscard]] BGPU_API TextureHandle
	ImportD3d12Texture(
		IResourceManager&  resourceManager,
		ID3D12Resource*    resource,
		const TextureDesc& desc) noexcept;
}

#endif
