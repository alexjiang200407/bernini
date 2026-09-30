#include "cmd/CommandQueue_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/d3d12/native_rhi.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>

namespace bgpu
{
	ID3D12CommandQueue*
	GetD3d12CommandQueue(const ICommandQueue& queue) noexcept
	{
		return static_cast<const CommandQueue&>(queue).GetD3D12CommandQueue();
	}

	TextureHandle
	ImportD3d12Texture(
		IResourceManager&  resourceManager,
		ID3D12Resource*    resource,
		const TextureDesc& desc) noexcept
	{
		return resourceManager.As<ResourceManager>()->CreateTexture(
			wrl::ComPtr<ID3D12Resource>(resource),
			desc);
	}
}
