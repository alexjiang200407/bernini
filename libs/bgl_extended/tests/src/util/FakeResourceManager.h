#pragma once
#include "resource/ResourceManager.h"

namespace bgl::test
{
	/**
	 * An IResourceManager that really tracks textures and views, on the CPU, with no device behind
	 * it: it mints slots, answers ValidTextureHandle from the live set, and records every create and
	 * destroy so a test can assert what a subject released.
	 *
	 * Only the texture and view half is implemented -- everything else aborts if called, so a
	 * subject that strays outside that half fails loudly rather than reading a zeroed handle.
	 */
	class FakeResourceManager : public core::RefCounter<IResourceManager>
	{
	public:
		FakeResourceManager()                           = default;
		FakeResourceManager(const FakeResourceManager&) = delete;
		FakeResourceManager(FakeResourceManager&&)      = delete;

		FakeResourceManager&
		operator=(const FakeResourceManager&) = delete;

		FakeResourceManager&
		operator=(FakeResourceManager&&) = delete;

		// Fails the next CreateTexture, then clears itself: the pool-exhausted path.
		bool failNextTexture = false;

		// Fails the next CreateSrv, then clears itself: the descriptor-exhausted path, which is the
		// one that must take its already-created texture back down with it.
		bool failNextSrv = false;

		std::vector<TextureDesc>   createdTextures;
		std::vector<TextureHandle> destroyedTextures;
		std::vector<SrvHandle>     destroyedSrvs;

		[[nodiscard]] bool
		IsTextureLive(TextureHandle handle) const noexcept
		{
			return m_LiveTextures.contains(handle.slot.index);
		}

		[[nodiscard]] size_t
		LiveTextureCount() const noexcept
		{
			return m_LiveTextures.size();
		}

		TextureHandle
		CreateTexture(const TextureDesc& desc) noexcept override
		{
			if (failNextTexture)
			{
				failNextTexture = false;
				return {};
			}

			createdTextures.push_back(desc);

			const uint32_t index = m_NextTextureIndex++;
			m_LiveTextures.insert(index);
			return TextureHandle{ core::slot_handle{ index, 1 } };
		}

		SrvHandle
		CreateSrv(TextureHandle, const SrvDesc&) noexcept override
		{
			if (failNextSrv)
			{
				failNextSrv = false;
				return {};
			}

			const uint32_t index = m_NextSrvIndex++;

			auto srv          = SrvHandle();
			srv.idx           = index;
			srv.generation    = 1;
			srv.bindlessIndex = index;
			srv.descriptor    = DescriptorHandle{ index };
			return srv;
		}

		void
		DestroyTexture(TextureHandle handle, bool) noexcept override
		{
			destroyedTextures.push_back(handle);
			m_LiveTextures.erase(handle.slot.index);
		}

		void
		DestroySrv(SrvHandle handle, bool) noexcept override
		{
			destroyedSrvs.push_back(handle);
		}

		bool
		ValidTextureHandle(const TextureHandle& handle) const noexcept override
		{
			return IsTextureLive(handle);
		}

		BufferHandle
		CreateStructBuffer(const StructBufferDesc&) noexcept override
		{
			std::abort();
		}
		BufferHandle
		CreateComputeBuffer(const ComputeBufferDesc&) noexcept override
		{
			std::abort();
		}
		BufferHandle
		CreateRawBuffer(const RawViewDesc&) noexcept override
		{
			std::abort();
		}
		BufferSrvHandle
		CreateBufferSrv(BufferHandle, const BufferSrvDesc&) noexcept override
		{
			std::abort();
		}
		void
		DestroyBufferSrv(BufferSrvHandle, bool) noexcept override
		{
			std::abort();
		}
		bool
		ValidBufferSrvHandle(const BufferSrvHandle&) const noexcept override
		{
			return false;
		}
		SamplerHandle
		CreateSampler(const SamplerDesc&) noexcept override
		{
			std::abort();
		}
		ReadbackBufferHandle
		CreateReadbackBuffer(const ReadbackBufferDesc&) noexcept override
		{
			std::abort();
		}
		void
		RegisterQueue(ICommandQueue*) noexcept override
		{}
		void
		UnregisterQueue(ICommandQueue*) noexcept override
		{}
		void
		DestroyBuffer(BufferHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroySampler(SamplerHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyReadbackBuffer(ReadbackBufferHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyRtv(RtvHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyDsv(DsvHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		CleanupExpiredResources() noexcept override
		{}
		RtvHandle
		CreateRtv(TextureHandle, const RtvDesc&) noexcept override
		{
			std::abort();
		}
		DsvHandle
		CreateDsv(TextureHandle, const DsvDesc&) noexcept override
		{
			std::abort();
		}
		const Rtv&
		GetRtv(RtvHandle) const noexcept override
		{
			std::abort();
		}
		const Dsv&
		GetDsv(DsvHandle) const noexcept override
		{
			std::abort();
		}
		TextureHandle
		GetRtvTexture(RtvHandle) const noexcept override
		{
			std::abort();
		}
		TextureHandle
		GetDsvTexture(DsvHandle) const noexcept override
		{
			std::abort();
		}
		const Buffer&
		GetBuffer(BufferHandle) const noexcept override
		{
			std::abort();
		}
		BufferDesc
		GetBufferDesc(BufferHandle) const noexcept override
		{
			std::abort();
		}
		const Texture&
		GetTexture(TextureHandle) const noexcept override
		{
			std::abort();
		}
		TextureDesc
		GetTextureDesc(TextureHandle) const noexcept override
		{
			std::abort();
		}
		const Sampler&
		GetSampler(SamplerHandle) const noexcept override
		{
			std::abort();
		}
		const ReadbackBuffer&
		GetReadbackBuffer(ReadbackBufferHandle) const noexcept override
		{
			std::abort();
		}
		TextureReadbackLayout
		GetTextureReadbackLayout(TextureHandle) const noexcept override
		{
			std::abort();
		}
		const void*
		MapReadback(ReadbackBufferHandle) noexcept override
		{
			std::abort();
		}
		void
		UnmapReadback(ReadbackBufferHandle) noexcept override
		{
			std::abort();
		}
		bool
		ValidBufferHandle(const BufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		IsTextureCube(const TextureHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidSrvHandle(const SrvHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidSamplerHandle(const SamplerHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidReadbackBufferHandle(const ReadbackBufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidRtvHandle(const RtvHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidDsvHandle(const DsvHandle&) const noexcept override
		{
			return false;
		}
		void
		ClearRtv(ICommandList*, RtvHandle, float[4]) noexcept override
		{
			std::abort();
		}
		void
		ClearDsv(ICommandList*, DsvHandle, float, uint8_t) noexcept override
		{
			std::abort();
		}

	private:
		// Starts at 1 so a default-constructed handle's slot never collides with a live one.
		uint32_t                     m_NextTextureIndex = 1;
		uint32_t                     m_NextSrvIndex     = 1;
		std::unordered_set<uint32_t> m_LiveTextures;
	};
}
