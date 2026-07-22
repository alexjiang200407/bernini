#pragma once
#include "device/Device.h"
#include "resource/Buffer.h"
#include "resource/Dsv.h"
#include "resource/Readback.h"
#include "resource/Rtv.h"
#include "resource/Sampler.h"
#include "resource/Texture.h"
#include "types/ClearValue.h"

#include <assetlib_structs/ImageData.h>

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct ResourceManagerDesc
	{
		uint32_t maxCbvSrvUavs = 1024;
		uint32_t maxRtvs       = 128;
		uint32_t maxDsvs       = 128;
		uint32_t maxTextures   = 1024;
		uint32_t maxSamplers   = 128;
	};

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
		CreateStructBuffer(const StructBufferDesc& desc) noexcept = 0;

		virtual BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept = 0;

		// Creation is upload-free: the manager makes resources and descriptors, never issues
		// copies. A caller with pixel data creates the texture, keeps the bytes, and writes them
		// on its own command list (ICommandList::WriteTexture) -- Scene's pending-upload queue is
		// the pattern -- so the upload is ordered against the frames that sample it.
		virtual TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept = 0;

		[[nodiscard]]
		virtual SamplerHandle
		CreateSampler(const SamplerDesc& desc) noexcept = 0;

		// Creates a CPU-readable buffer in the readback heap, used as the
		// destination of GPU->CPU copies.
		virtual ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept = 0;

		/**
		 * Registers a submission queue as one of the timelines a deferred destroy must clear before
		 * its resource is reclaimed. Each context registers its own queue; the manager snapshots
		 * every registered queue at destroy time and frees a resource only once all of them pass.
		 *
		 * @pre the queue outlives its registration -- unregister before it is destroyed.
		 */
		virtual void
		RegisterQueue(ICommandQueue* queue) noexcept = 0;

		/**
		 * Removes a queue from the timeline set. Its context has flushed and is going away, so any
		 * pending free still gated on it is now satisfiable -- CleanupExpiredResources treats a gate
		 * entry whose queue is no longer registered as complete.
		 */
		virtual void
		UnregisterQueue(ICommandQueue* queue) noexcept = 0;

		/**
		 * Destroys a resource. `deferred` (the default) retires it now -- staling every handle at
		 * once -- and reclaims the slot only after every registered queue passes the fence it was at
		 * when this was called; the manager resolves those fences itself, so there is no value to get
		 * wrong. `deferred = false` frees immediately and is only safe when the GPU is already idle
		 * for the resource (e.g. after a Flush during resize or teardown).
		 */
		virtual void
		DestroyBuffer(BufferHandle handle, bool deferred = true) noexcept = 0;

		virtual void
		DestroyTexture(TextureHandle handle, bool deferred = true) noexcept = 0;

		virtual void
		DestroySampler(SamplerHandle handle, bool deferred = true) noexcept = 0;

		virtual void
		DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred = true) noexcept = 0;

		virtual void
		DestroyRtv(RtvHandle handle, bool deferred = true) noexcept = 0;

		virtual void
		DestroyDsv(DsvHandle handle, bool deferred = true) noexcept = 0;

		/**
		 * Reclaims every deferred-destroyed resource whose gate has cleared on all registered queues.
		 * Polls each queue once; call it periodically (each frame's EndFrame does).
		 */
		virtual void
		CleanupExpiredResources() noexcept = 0;

		[[nodiscard]]
		virtual RtvHandle
		CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept = 0;

		[[nodiscard]]
		virtual DsvHandle
		CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept = 0;

		[[nodiscard]]
		virtual const Rtv&
		GetRtv(RtvHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const Dsv&
		GetDsv(DsvHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual TextureHandle
		GetRtvTexture(RtvHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual TextureHandle
		GetDsvTexture(DsvHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const Buffer&
		GetBuffer(BufferHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const Texture&
		GetTexture(TextureHandle handle) const noexcept = 0;

		// A texture's dimensions, format and usage, without exposing the backend Texture -- so a
		// caller in backend-agnostic code can read them (the concrete Texture is an incomplete type
		// there).
		[[nodiscard]]
		virtual TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const Sampler&
		GetSampler(SamplerHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept = 0;

		// Row-pitch layout of texture subresource 0 within a readback buffer.
		[[nodiscard]]
		virtual TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle handle) const noexcept = 0;

		// Maps a readback buffer for CPU reading; valid until UnmapReadback.
		[[nodiscard]]
		virtual const void*
		MapReadback(ReadbackBufferHandle handle) noexcept = 0;

		virtual void
		UnmapReadback(ReadbackBufferHandle handle) noexcept = 0;

		[[nodiscard]] virtual bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept = 0;

		// True if the texture is a cube map (or cube-map array). The handle must be
		// valid (checked via ValidTextureHandle first).
		[[nodiscard]] virtual bool
		IsTextureCube(const TextureHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual bool
		ValidSamplerHandle(const SamplerHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual bool
		ValidRtvHandle(const RtvHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual bool
		ValidDsvHandle(const DsvHandle& handle) const noexcept = 0;

		virtual void
		ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept = 0;

		virtual void
		ClearDsv(
			ICommandList* cmdList,
			DsvHandle     handle,
			float         depth,
			uint8_t       stencil) noexcept = 0;
	};

	using ResourceManagerRef = core::SharedRef<IResourceManager>;
}
