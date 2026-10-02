#pragma once
#include <bgpu/device/Device.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/ClearValue.h>
#include <bgpu/types/NativeObject.h>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>

#include <core/ref/SharedRef.h>
#include <cstdint>

namespace bgpu
{
	/**
	 * The pool sizes are the renderer's. Every pool but maxBuffers may be zero, which is how an
	 * owner says it makes none of that kind: no heap is created for it, and every create from it
	 * fails as an exhausted pool does. A compute owner starts from ComputeOnly().
	 */
	struct ResourceManagerDesc
	{
		// Descriptors in the shader-visible heap. Every buffer, every SRV and every second view of a
		// buffer takes one, and index 0 is burned as the unbound sentinel, so it must be at least
		// maxBuffers + maxSrvs + maxBufferSrvs + maxBufferUavs + 1.
		uint32_t maxCbvSrvUavs = 1105;

		uint32_t maxBuffers = 512;
		uint32_t maxSrvs    = 512;

		// Second, structured views of buffers (CreateBufferSrv). Far fewer than buffers: only an
		// arena whose records hold resource handles needs one.
		uint32_t maxBufferSrvs = 64;

		// Writable second views of read-only buffers (CreateBufferUav). Fewer still: only a buffer
		// one pass writes and every other pass reads needs one.
		uint32_t maxBufferUavs      = 16;
		uint32_t maxRtvs            = 128;
		uint32_t maxDsvs            = 128;
		uint32_t maxTextures        = 1024;
		uint32_t maxSamplers        = 128;
		uint32_t maxReadbackBuffers = 64;

		/** Buffers, their second views and readbacks at the default sizes; no texture of any kind. */
		[[nodiscard]] static constexpr ResourceManagerDesc
		ComputeOnly() noexcept
		{
			auto desc          = ResourceManagerDesc();
			desc.maxSrvs       = 0;
			desc.maxRtvs       = 0;
			desc.maxDsvs       = 0;
			desc.maxTextures   = 0;
			desc.maxSamplers   = 0;
			desc.maxCbvSrvUavs = desc.maxBuffers + desc.maxBufferSrvs + desc.maxBufferUavs + 1;
			return desc;
		}
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

		/**
		 * Creates a buffer a shader addresses by byte rather than by element, viewed as a
		 * ByteAddressBuffer (`RawBuffer` in Slang) or, with `isUav`, an RWByteAddressBuffer.
		 *
		 * @pre byteSize is a non-zero multiple of 4 and at most c_MaxRawBufferBytes -- a raw view
		 * addresses bytes with a uint, so nothing beyond that is reachable however big the
		 * allocation is.
		 * @post the buffer's own view is raw, so one bound to a StructuredBuffer uniform (or the
		 * reverse) reads undefined bytes rather than failing. A second, structured view of the same
		 * bytes may be added with CreateBufferSrv.
		 */
		virtual BufferHandle
		CreateRawBuffer(const RawViewDesc& desc) noexcept = 0;

		/**
		 * A second, structured view of a buffer that already has one, for data its first view
		 * cannot type.
		 *
		 * A raw view reads bytes, and a bindless resource handle cannot be made from bytes on every
		 * backend (see docs/rhi.md): a record holding one stores it as plain bytes and reads it
		 * back through a view of this kind, over the same allocation.
		 *
		 * @pre the buffer is valid and its byte size is a multiple of `stride`.
		 * @post destroying the buffer does not destroy the view, as with an Srv onto a texture.
		 * @post the view describes the resource the buffer holds *now*, and a growth replaces that
		 * resource without announcing it, so whoever owns the buffer re-issues the view with it.
		 */
		[[nodiscard]]
		virtual BufferSrvHandle
		CreateBufferSrv(BufferHandle buffer, const BufferSrvDesc& desc) noexcept = 0;

		virtual void
		DestroyBufferSrv(BufferSrvHandle handle, bool deferred = true) noexcept = 0;

		[[nodiscard]] virtual bool
		ValidBufferSrvHandle(const BufferSrvHandle& handle) const noexcept = 0;

		/**
		 * A writable view of a buffer every other reader sees read-only, for the pass that writes
		 * it while the rest of the frame reads it through its own view.
		 *
		 * Barriers are the caller's, as for any buffer: a write through this view is a
		 * kUnorderedAccess access to the same resource.
		 *
		 * @pre the buffer is valid, structured, created with `allowsUav` or `isUav`, and its byte
		 * size is a multiple of `desc.stride`.
		 * @post destroying the buffer does not destroy the view, and a growth replaces the resource
		 * the view describes, so whoever owns the buffer re-issues the view with it -- as with
		 * CreateBufferSrv.
		 */
		[[nodiscard]]
		virtual BufferUavHandle
		CreateBufferUav(BufferHandle buffer, const BufferUavDesc& desc) noexcept = 0;

		virtual void
		DestroyBufferUav(BufferUavHandle handle, bool deferred = true) noexcept = 0;

		[[nodiscard]] virtual bool
		ValidBufferUavHandle(const BufferUavHandle& handle) const noexcept = 0;

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
		DestroySrv(SrvHandle handle, bool deferred = true) noexcept = 0;

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

		/**
		 * Makes a texture readable by a shader, and is the only thing that does.
		 *
		 * A texture on its own has no descriptor: CreateTexture allocates storage, nothing more. The
		 * returned handle carries what a shader needs to reach it, so a texture that is only ever a
		 * render target has no bindless index to be wrong about.
		 *
		 * Destroying the texture does not destroy its views. Release both, as with an Rtv.
		 *
		 * @param textureHandle a valid texture whose TextureDesc allowed TextureUsageFlag::kSRV.
		 */
		[[nodiscard]]
		virtual SrvHandle
		CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept = 0;

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

		// What the buffer was allocated as, without exposing the backend Buffer -- so a caller in
		// backend-agnostic code can read it (the concrete Buffer is an incomplete type there), as
		// with GetTextureDesc.
		[[nodiscard]]
		virtual BufferDesc
		GetBufferDesc(BufferHandle handle) const noexcept = 0;

		[[nodiscard]]
		virtual const Texture&
		GetTexture(TextureHandle handle) const noexcept = 0;

		// A texture's dimensions, format and usage, without exposing the backend Texture -- so a
		// caller in backend-agnostic code can read them (the concrete Texture is an incomplete type
		// there).
		[[nodiscard]]
		virtual TextureDesc
		GetTextureDesc(TextureHandle handle) const noexcept = 0;

		/**
		 * The native texture behind `handle` as `type`, or null when this backend has none of that
		 * type. Borrowed: destroying the texture ends it.
		 */
		[[nodiscard]] virtual NativeObject
		GetNativeTexture(TextureHandle handle, NativeObjectType type) const noexcept
		{
			(void)handle;
			(void)type;
			return {};
		}

		/**
		 * Adopts a texture made outside this manager -- a swapchain's backbuffer -- so it is viewed,
		 * barriered and destroyed like any other. The manager adds its own reference, and destroying
		 * the texture releases only that. Null when this backend cannot adopt a `desc.type`.
		 *
		 * @pre `desc.texture` describes the object, and `desc.texture.initialLayout` is the layout it
		 *      is in now.
		 */
		[[nodiscard]] virtual TextureHandle
		ImportNativeTexture(const NativeTextureDesc& desc) noexcept
		{
			(void)desc;
			return {};
		}

		/**
		 * The native buffer behind `handle` as `type`, or null when this backend has none of that
		 * type. Borrowed: destroying the buffer ends it. What another owner's ImportNativeBuffer
		 * adopts -- see NativeBufferDesc.
		 */
		[[nodiscard]] virtual NativeObject
		GetNativeBuffer(BufferHandle handle, NativeObjectType type) const noexcept
		{
			(void)handle;
			(void)type;
			return {};
		}

		/**
		 * Adopts a buffer another owner on the same native device made, as a read-only structured
		 * buffer as `desc.buffer` describes it. The bytes are shared, not
		 * copied: this manager adds its own reference and a descriptor in its own heap, so the
		 * memory outlives the producer's release until this handle is destroyed too. Null when this
		 * backend cannot adopt a `desc.type`.
		 *
		 * Nothing orders the producer's writes against this owner's reads. A reader waits on the
		 * producer's queue (ICommandQueue::InsertWaitForQueueFence) before it reads, and a producer
		 * that reuses the memory waits on the reader's -- see docs/rhi.md.
		 *
		 * @pre `desc.object` is a buffer on this manager's native device, at least
		 *      `desc.buffer.stride * desc.buffer.elementCount` bytes long; `desc.buffer.isUav` is
		 *      false.
		 */
		[[nodiscard]] virtual BufferHandle
		ImportNativeBuffer(const NativeBufferDesc& desc) noexcept
		{
			(void)desc;
			return {};
		}

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
		ValidSrvHandle(const SrvHandle& handle) const noexcept = 0;

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
