#pragma once
#include "resource/Buffer.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	class ResourceManagerImpl;
	class ResourceManager : public core::RefCountPImpl<ResourceManagerImpl>
	{
	public:
		ResourceManager() = default;

		BufferHandle
		CreateRawBuffer(const BufferDesc& desc);

		TextureHandle
		CreateTexture(const TextureDesc& desc);

		void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue);

		void
		DestroyTexture(TextureHandle handle, uint64_t currentFenceValue);

		void
		CleanupExpiredResources(uint64_t completedFenceValue);

		[[nodiscard]]
		RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc);

		[[nodiscard]]
		const Rtv&
		GetRtv(const RtvHandle& handle) const;

		[[nodiscard]]
		const Buffer&
		GetBuffer(const BufferHandle& handle) const;

		[[nodiscard]]
		const Texture&
		GetTexture(const TextureHandle& handle) const;

		friend class DeviceImpl;
	};
}
