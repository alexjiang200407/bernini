#pragma once
#include "device/Device.h"
#include "resource/Buffer.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IResourceManager : public core::Ref
	{
	public:
		IResourceManager()                                 = default;
		IResourceManager(const IResourceManager&) noexcept = delete;
		IResourceManager(IResourceManager&&) noexcept      = delete;

		IResourceManager&
		operator=(const IResourceManager&) noexcept = delete;

		IResourceManager&
		operator=(IResourceManager&&) noexcept = delete;

		virtual BufferHandle
		CreateStructBuffer(const BufferDesc& desc) = 0;

		virtual TextureHandle
		CreateTexture(const TextureDesc& desc) = 0;

		virtual void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue, bool deferred = true) = 0;

		virtual void
		DestroyTexture(TextureHandle handle, uint64_t currentFenceValue, bool deferred = true) = 0;

		virtual void
		DestroyRtv(RtvHandle handle, uint64_t currentFenceValue, bool deferred = true) = 0;

		virtual void
		CleanupExpiredResources(uint64_t completedFenceValue) = 0;

		[[nodiscard]]
		virtual RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) = 0;

		[[nodiscard]]
		virtual const Rtv&
		GetRtv(RtvHandle handle) const = 0;

		[[nodiscard]]
		virtual const Buffer&
		GetBuffer(BufferHandle handle) const = 0;

		[[nodiscard]]
		virtual const Texture&
		GetTexture(TextureHandle handle) const = 0;

		[[nodiscard]] virtual bool
		ValidBufferHandle(const BufferHandle& handle) const = 0;

		[[nodiscard]] virtual bool
		ValidTextureHandle(const TextureHandle& handle) const = 0;

		[[nodiscard]] virtual bool
		ValidRtvHandle(const RtvHandle& handle) const = 0;
	};

	using ResourceManagerHandle = core::SharedRef<IResourceManager>;
}
