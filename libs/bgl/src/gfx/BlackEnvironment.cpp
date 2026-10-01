#include "gfx/BlackEnvironment.h"
#include "types/EnvironmentMap.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/TextureDimension.h>
#include <core/err/util.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr bgpu::Format c_Format        = bgpu::Format::RGBA16_FLOAT;
		constexpr uint64_t     c_BytesPerTexel = 8;
		constexpr uint32_t     c_CubeFaces     = 6;

		// One texel of zero, shared by every subresource: a face is a single texel.
		constexpr std::array<uint16_t, 4> c_Black = { { 0, 0, 0, 0 } };

		struct Created
		{
			bgpu::TextureHandle texture;
			bgpu::SrvHandle     srv;
		};

		Created
		Create(
			bgpu::IResourceManager& resourceManager,
			bgpu::TextureDimension  dimension,
			uint32_t                arraySize)
		{
			const std::string name = dimension == bgpu::TextureDimension::kTextureCube ?
			                             "Black environment" :
			                             "Black BRDF LUT";

			auto desc          = bgpu::TextureDesc();
			desc.arraySize     = arraySize;
			desc.format        = c_Format;
			desc.usage         = bgpu::TextureUsageFlag::kSRV;
			desc.dimension     = dimension;
			desc.initialLayout = bgpu::BarrierLayout::kCopyDest;
			desc.debugName     = name;

			const bgpu::TextureHandle texture = resourceManager.CreateTexture(desc);
			if (texture.IsNull())
				throw GraphicsError(name + " texture could not be created");

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_Format;
			srvDesc.dimension = dimension;
			srvDesc.arraySize = arraySize;
			srvDesc.debugName = name + " SRV";

			const bgpu::SrvHandle srv = resourceManager.CreateSrv(texture, srvDesc);
			if (srv.IsNull())
			{
				resourceManager.DestroyTexture(texture, false);
				throw GraphicsError(name + " SRV could not be created");
			}
			return Created{ texture, srv };
		}

		void
		Fill(bgpu::ICommandList& cmdList, bgpu::TextureHandle texture, uint32_t subresources)
		{
			const bgpu::TextureSubresourceData                          texel{ c_Black.data(),
				                                                               c_BytesPerTexel,
				                                                               c_BytesPerTexel };
			const std::array<bgpu::TextureSubresourceData, c_CubeFaces> data = {
				{ texel, texel, texel, texel, texel, texel }
			};
			cmdList.WriteTexture(texture, { data.data(), subresources });

			bgpu::TextureBarrierDesc barrier;
			barrier.syncBefore   = bgpu::BarrierSyncFlag::kCopy;
			barrier.accessBefore = bgpu::BarrierAccessFlag::kCopyDest;
			barrier.layoutBefore = bgpu::BarrierLayout::kCopyDest;
			barrier.syncAfter =
				bgpu::BarrierSyncFlag::kPixelShader | bgpu::BarrierSyncFlag::kComputeShader;
			barrier.accessAfter = bgpu::BarrierAccessFlag::kShaderResource;
			barrier.layoutAfter = bgpu::BarrierLayout::kShaderResource;
			cmdList.Barrier(texture, barrier);
		}
	}

	BlackEnvironment::BlackEnvironment(bgpu::ResourceManagerRef resourceManager) :
		m_ResourceManager(std::move(resourceManager))
	{
		const Created cube =
			Create(*m_ResourceManager, bgpu::TextureDimension::kTextureCube, c_CubeFaces);
		m_Cube    = cube.texture;
		m_CubeSrv = cube.srv;

		try
		{
			const Created lut = Create(*m_ResourceManager, bgpu::TextureDimension::kTexture2D, 1);
			m_Lut             = lut.texture;
			m_LutSrv          = lut.srv;
		}
		catch (...)
		{
			Free();
			throw;
		}
	}

	BlackEnvironment::~BlackEnvironment() noexcept
	{
		spdlog::trace("~BlackEnvironment");
		Free();
	}

	void
	BlackEnvironment::Upload(bgpu::ICommandList* cmdList)
	{
		core::ensure(cmdList != nullptr, "Command list must be initialized");

		Fill(*cmdList, m_Cube, c_CubeFaces);
		Fill(*cmdList, m_Lut, 1);
	}

	EnvironmentMap
	BlackEnvironment::Complete(EnvironmentMap env) const noexcept
	{
		if (env.irradiance.IsNull())
			env.irradiance = m_CubeSrv;
		if (env.prefilter.IsNull())
			env.prefilter = m_CubeSrv;
		if (env.brdfLut.IsNull())
			env.brdfLut = m_LutSrv;
		return env;
	}

	void
	BlackEnvironment::Free() noexcept
	{
		for (bgpu::SrvHandle* srv : { &m_CubeSrv, &m_LutSrv })
		{
			if (!srv->IsNull())
			{
				m_ResourceManager->DestroySrv(*srv);
				*srv = bgpu::SrvHandle{};
			}
		}
		for (bgpu::TextureHandle* texture : { &m_Cube, &m_Lut })
		{
			if (!texture->IsNull())
			{
				m_ResourceManager->DestroyTexture(*texture);
				*texture = bgpu::TextureHandle{};
			}
		}
	}
}
