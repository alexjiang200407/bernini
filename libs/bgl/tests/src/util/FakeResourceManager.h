#pragma once
#include <bgpu/device/Device.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <core/ref/RefCounter.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>
#include <vector>

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
	class FakeResourceManager : public core::RefCounter<bgpu::IResourceManager>
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

		std::vector<bgpu::TextureDesc>   createdTextures;
		std::vector<bgpu::TextureHandle> destroyedTextures;
		std::vector<bgpu::SrvHandle>     destroyedSrvs;

		[[nodiscard]] bool
		IsTextureLive(bgpu::TextureHandle handle) const noexcept
		{
			return m_LiveTextures.contains(handle.slot.index);
		}

		[[nodiscard]] size_t
		LiveTextureCount() const noexcept
		{
			return m_LiveTextures.size();
		}

		bgpu::TextureHandle
		CreateTexture(const bgpu::TextureDesc& desc) noexcept override
		{
			if (failNextTexture)
			{
				failNextTexture = false;
				return {};
			}

			createdTextures.push_back(desc);

			const uint32_t index = m_NextTextureIndex++;
			m_LiveTextures.insert(index);
			return bgpu::TextureHandle{ core::slot_handle{ index, 1 } };
		}

		bgpu::SrvHandle
		CreateSrv(bgpu::TextureHandle, const bgpu::SrvDesc&) noexcept override
		{
			if (failNextSrv)
			{
				failNextSrv = false;
				return {};
			}

			const uint32_t index = m_NextSrvIndex++;

			auto srv          = bgpu::SrvHandle();
			srv.idx           = index;
			srv.generation    = 1;
			srv.bindlessIndex = index;
			srv.descriptor    = bgpu::DescriptorHandle{ index };
			return srv;
		}

		void
		DestroyTexture(bgpu::TextureHandle handle, bool) noexcept override
		{
			destroyedTextures.push_back(handle);
			m_LiveTextures.erase(handle.slot.index);
		}

		void
		DestroySrv(bgpu::SrvHandle handle, bool) noexcept override
		{
			destroyedSrvs.push_back(handle);
		}

		bool
		ValidTextureHandle(const bgpu::TextureHandle& handle) const noexcept override
		{
			return IsTextureLive(handle);
		}

		bgpu::BufferHandle
		CreateStructBuffer(const bgpu::StructBufferDesc&) noexcept override
		{
			std::abort();
		}
		bgpu::BufferHandle
		CreateComputeBuffer(const bgpu::ComputeBufferDesc&) noexcept override
		{
			std::abort();
		}
		bgpu::BufferHandle
		CreateRawBuffer(const bgpu::RawViewDesc&) noexcept override
		{
			std::abort();
		}
		bgpu::BufferSrvHandle
		CreateBufferSrv(bgpu::BufferHandle, const bgpu::BufferSrvDesc&) noexcept override
		{
			std::abort();
		}
		void
		DestroyBufferSrv(bgpu::BufferSrvHandle, bool) noexcept override
		{
			std::abort();
		}
		bool
		ValidBufferSrvHandle(const bgpu::BufferSrvHandle&) const noexcept override
		{
			return false;
		}
		bgpu::BufferUavHandle
		CreateBufferUav(bgpu::BufferHandle, const bgpu::BufferUavDesc&) noexcept override
		{
			std::abort();
		}
		void
		DestroyBufferUav(bgpu::BufferUavHandle, bool) noexcept override
		{
			std::abort();
		}
		bool
		ValidBufferUavHandle(const bgpu::BufferUavHandle&) const noexcept override
		{
			return false;
		}
		bgpu::SamplerHandle
		CreateSampler(const bgpu::SamplerDesc&) noexcept override
		{
			std::abort();
		}
		bgpu::ReadbackBufferHandle
		CreateReadbackBuffer(const bgpu::ReadbackBufferDesc&) noexcept override
		{
			std::abort();
		}
		void
		RegisterQueue(bgpu::ICommandQueue*) noexcept override
		{}
		void
		UnregisterQueue(bgpu::ICommandQueue*) noexcept override
		{}
		void
		DestroyBuffer(bgpu::BufferHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroySampler(bgpu::SamplerHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyReadbackBuffer(bgpu::ReadbackBufferHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyRtv(bgpu::RtvHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		DestroyDsv(bgpu::DsvHandle, bool) noexcept override
		{
			std::abort();
		}
		void
		CleanupExpiredResources() noexcept override
		{}
		bgpu::RtvHandle
		CreateRtv(bgpu::TextureHandle, const bgpu::RtvDesc&) noexcept override
		{
			std::abort();
		}
		bgpu::DsvHandle
		CreateDsv(bgpu::TextureHandle, const bgpu::DsvDesc&) noexcept override
		{
			std::abort();
		}
		const bgpu::Rtv&
		GetRtv(bgpu::RtvHandle) const noexcept override
		{
			std::abort();
		}
		const bgpu::Dsv&
		GetDsv(bgpu::DsvHandle) const noexcept override
		{
			std::abort();
		}
		bgpu::TextureHandle
		GetRtvTexture(bgpu::RtvHandle) const noexcept override
		{
			std::abort();
		}
		bgpu::TextureHandle
		GetDsvTexture(bgpu::DsvHandle) const noexcept override
		{
			std::abort();
		}
		const bgpu::Buffer&
		GetBuffer(bgpu::BufferHandle) const noexcept override
		{
			std::abort();
		}
		bgpu::BufferDesc
		GetBufferDesc(bgpu::BufferHandle) const noexcept override
		{
			std::abort();
		}
		const bgpu::Texture&
		GetTexture(bgpu::TextureHandle) const noexcept override
		{
			std::abort();
		}
		bgpu::TextureDesc
		GetTextureDesc(bgpu::TextureHandle) const noexcept override
		{
			std::abort();
		}
		const bgpu::Sampler&
		GetSampler(bgpu::SamplerHandle) const noexcept override
		{
			std::abort();
		}
		const bgpu::ReadbackBuffer&
		GetReadbackBuffer(bgpu::ReadbackBufferHandle) const noexcept override
		{
			std::abort();
		}
		bgpu::TextureReadbackLayout
		GetTextureReadbackLayout(bgpu::TextureHandle) const noexcept override
		{
			std::abort();
		}
		const void*
		MapReadback(bgpu::ReadbackBufferHandle) noexcept override
		{
			std::abort();
		}
		void
		UnmapReadback(bgpu::ReadbackBufferHandle) noexcept override
		{
			std::abort();
		}
		bool
		ValidBufferHandle(const bgpu::BufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		IsTextureCube(const bgpu::TextureHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidSrvHandle(const bgpu::SrvHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidSamplerHandle(const bgpu::SamplerHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidReadbackBufferHandle(const bgpu::ReadbackBufferHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidRtvHandle(const bgpu::RtvHandle&) const noexcept override
		{
			return false;
		}
		bool
		ValidDsvHandle(const bgpu::DsvHandle&) const noexcept override
		{
			return false;
		}
		void
		ClearRtv(bgpu::ICommandList*, bgpu::RtvHandle, float[4]) noexcept override
		{
			std::abort();
		}
		void
		ClearDsv(bgpu::ICommandList*, bgpu::DsvHandle, float, uint8_t) noexcept override
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
