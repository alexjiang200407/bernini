#pragma once
#include "metal_cpp.h"

#include "resource/Buffer_metal.h"
#include "resource/ReadbackBuffer_metal.h"

#include "resource/ResourceManager.h"

#include <core/containers/slot_vector.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The Metal resource manager. This slice owns only buffers and readback buffers -- the pieces the
	 * headless compute/readback path needs. Textures, samplers, RTV/DSV and the bindless argument
	 * buffer arrive with the render and scene slices; those factories gfatal for now.
	 */
	class ResourceManager final : public core::RefCounter<IResourceManager>
	{
	public:
		ResourceManager(MTL::Device* device, const ResourceManagerDesc& desc);

		[[nodiscard]] MTL::Device*
		GetMTLDevice() const noexcept
		{
			return m_Device;
		}

		BufferHandle
		CreateStructBuffer(const StructBufferDesc& desc) noexcept override;

		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept override;

		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept override;

		void
		DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue, bool deferred) noexcept
			override;

		void
		DestroyReadbackBuffer(
			ReadbackBufferHandle handle,
			uint64_t             currentFenceValue,
			bool                 deferred) noexcept override;

		void
		CleanupExpiredResources(uint64_t completedFenceValue) noexcept override;

		const Buffer&
		GetBuffer(BufferHandle handle) const noexcept override;

		const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept override;

		const void*
		MapReadback(ReadbackBufferHandle handle) noexcept override;

		void
		UnmapReadback(ReadbackBufferHandle handle) noexcept override;

		[[nodiscard]] bool
		ValidBufferHandle(const BufferHandle& handle) const noexcept override;

		[[nodiscard]] bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept override;

		// ---- not yet implemented (render + scene slices) ----

		TextureHandle
		CreateTexture(const TextureDesc&) noexcept override
		{
			gfatal(k);
		}
		TextureHandle
		CreateTexture(const TextureDesc&, std::span<const TextureSubresourceData>) noexcept override
		{
			gfatal(k);
		}
		TextureHandle
		CreateTexture(const assetlib::ImageData&, std::string) noexcept override
		{
			gfatal(k);
		}
		TextureHandle
		CreateSolidTexture(uint8_t, uint8_t, uint8_t, uint8_t) noexcept override
		{
			gfatal(k);
		}
		SamplerHandle
		CreateSampler(const SamplerDesc&) noexcept override
		{
			gfatal(k);
		}
		void
		FlushPendingTextureUploads(ICommandList*) noexcept override
		{
			gfatal(k);
		}
		void
		DestroyTexture(TextureHandle, uint64_t, bool) noexcept override
		{
			gfatal(k);
		}
		void
		DestroyTexture(TextureHandle) noexcept override
		{
			gfatal(k);
		}
		void
		DestroySampler(SamplerHandle, uint64_t, bool) noexcept override
		{
			gfatal(k);
		}
		void
		DestroyRtv(RtvHandle, uint64_t, bool) noexcept override
		{
			gfatal(k);
		}
		void
		DestroyDsv(DsvHandle, uint64_t, bool) noexcept override
		{
			gfatal(k);
		}
		RtvHandle
		CreateRtv(TextureHandle, const RtvDesc&) noexcept override
		{
			gfatal(k);
		}
		DsvHandle
		CreateDsv(TextureHandle, const DsvDesc&) noexcept override
		{
			gfatal(k);
		}
		const Rtv&
		GetRtv(RtvHandle) const noexcept override
		{
			gfatal(k);
		}
		const Dsv&
		GetDsv(DsvHandle) const noexcept override
		{
			gfatal(k);
		}
		TextureHandle
		GetRtvTexture(RtvHandle) const noexcept override
		{
			gfatal(k);
		}
		TextureHandle
		GetDsvTexture(DsvHandle) const noexcept override
		{
			gfatal(k);
		}
		const Texture&
		GetTexture(TextureHandle) const noexcept override
		{
			gfatal(k);
		}
		const Sampler&
		GetSampler(SamplerHandle) const noexcept override
		{
			gfatal(k);
		}
		TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle) const noexcept override
		{
			gfatal(k);
		}
		bool
		ValidTextureHandle(const TextureHandle&) const noexcept override
		{
			gfatal(k);
		}
		bool
		IsTextureCube(const TextureHandle&) const noexcept override
		{
			gfatal(k);
		}
		bool
		ValidSamplerHandle(const SamplerHandle&) const noexcept override
		{
			gfatal(k);
		}
		bool
		ValidRtvHandle(const RtvHandle&) const noexcept override
		{
			gfatal(k);
		}
		bool
		ValidDsvHandle(const DsvHandle&) const noexcept override
		{
			gfatal(k);
		}
		void
		ClearRtv(ICommandList*, RtvHandle, float[4]) noexcept override
		{
			gfatal(k);
		}
		void
		ClearDsv(ICommandList*, DsvHandle, float, uint8_t) noexcept override
		{
			gfatal(k);
		}

	private:
		static constexpr const char* k =
			"Metal ResourceManager: not implemented yet (render/scene slice)";

		MTL::Device*                      m_Device = nullptr;
		core::slot_vector<Buffer>         m_Buffers;
		core::slot_vector<ReadbackBuffer> m_Readbacks;
	};
}
