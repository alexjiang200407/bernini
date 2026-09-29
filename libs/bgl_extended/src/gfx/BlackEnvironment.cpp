#include "gfx/BlackEnvironment.h"
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/EnvironmentMap.h"
#include "types/Format.h"
#include "types/TextureDimension.h"
#include <array>
#include <bgl/IGraphics.h>
#include <core/err/util.h>
#include <cstdint>
#include <string>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr Format   c_Format        = Format::RGBA16_FLOAT;
		constexpr uint64_t c_BytesPerTexel = 8;
		constexpr uint32_t c_CubeFaces     = 6;

		// One texel of zero, shared by every subresource: a face is a single texel.
		constexpr std::array<uint16_t, 4> c_Black = { { 0, 0, 0, 0 } };

		struct Created
		{
			TextureHandle texture;
			SrvHandle     srv;
		};

		Created
		Create(IResourceManager& resourceManager, TextureDimension dimension, uint32_t arraySize)
		{
			const std::string name = dimension == TextureDimension::kTextureCube ?
			                             "Black environment" :
			                             "Black BRDF LUT";

			auto desc          = TextureDesc();
			desc.arraySize     = arraySize;
			desc.format        = c_Format;
			desc.usage         = TextureUsageFlag::kSRV;
			desc.dimension     = dimension;
			desc.initialLayout = BarrierLayout::kCopyDest;
			desc.debugName     = name;

			const TextureHandle texture = resourceManager.CreateTexture(desc);
			if (texture.IsNull())
				throw GraphicsError(name + " texture could not be created");

			auto srvDesc      = SrvDesc();
			srvDesc.format    = c_Format;
			srvDesc.dimension = dimension;
			srvDesc.arraySize = arraySize;
			srvDesc.debugName = name + " SRV";

			const SrvHandle srv = resourceManager.CreateSrv(texture, srvDesc);
			if (srv.IsNull())
			{
				resourceManager.DestroyTexture(texture, false);
				throw GraphicsError(name + " SRV could not be created");
			}
			return Created{ texture, srv };
		}

		void
		Fill(ICommandList& cmdList, TextureHandle texture, uint32_t subresources)
		{
			const TextureSubresourceData texel{ c_Black.data(), c_BytesPerTexel, c_BytesPerTexel };
			const std::array<TextureSubresourceData, c_CubeFaces> data = {
				{ texel, texel, texel, texel, texel, texel }
			};
			cmdList.WriteTexture(texture, { data.data(), subresources });

			TextureBarrierDesc barrier;
			barrier.syncBefore   = BarrierSyncFlag::kCopy;
			barrier.accessBefore = BarrierAccessFlag::kCopyDest;
			barrier.layoutBefore = BarrierLayout::kCopyDest;
			barrier.syncAfter    = BarrierSyncFlag::kPixelShader | BarrierSyncFlag::kComputeShader;
			barrier.accessAfter  = BarrierAccessFlag::kShaderResource;
			barrier.layoutAfter  = BarrierLayout::kShaderResource;
			cmdList.Barrier(texture, barrier);
		}
	}

	void
	BlackEnvironment::Init(ResourceManagerRef resourceManager)
	{
		m_ResourceManager = std::move(resourceManager);

		const Created cube =
			Create(*m_ResourceManager, TextureDimension::kTextureCube, c_CubeFaces);
		m_Cube    = cube.texture;
		m_CubeSrv = cube.srv;

		const Created lut = Create(*m_ResourceManager, TextureDimension::kTexture2D, 1);
		m_Lut             = lut.texture;
		m_LutSrv          = lut.srv;
	}

	void
	BlackEnvironment::Upload(ICommandList* cmdList)
	{
		core::ensure(cmdList != nullptr, "Command list must be initialized");
		core::ensure(!m_Cube.IsNull() && !m_Lut.IsNull(), "BlackEnvironment::Upload before Init");

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
	BlackEnvironment::Release() noexcept
	{
		for (SrvHandle* srv : { &m_CubeSrv, &m_LutSrv })
		{
			if (!srv->IsNull())
			{
				m_ResourceManager->DestroySrv(*srv, false);
				*srv = SrvHandle{};
			}
		}
		for (TextureHandle* texture : { &m_Cube, &m_Lut })
		{
			if (!texture->IsNull())
			{
				m_ResourceManager->DestroyTexture(*texture, false);
				*texture = TextureHandle{};
			}
		}
	}
}
