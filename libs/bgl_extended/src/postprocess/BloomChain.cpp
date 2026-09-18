#include "postprocess/BloomChain.h"
#include "types/Format.h"
#include <algorithm>
#include <bgl_common/gassert.h>
#include <cstdint>
#include <format>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		// The scene colour's format: the ladder carries the same exposed linear radiance.
		constexpr Format c_BloomFormat = Format::RGBA16_FLOAT;

		// Six levels reaches a ~2000-pixel-wide glow on a 4K target, which is as wide as any
		// shipping preset; below eight texels a level is all filter footprint and adds ringing
		// rather than spread.
		constexpr uint32_t c_MaxBloomLevels = 6;
		constexpr uint32_t c_MinBloomExtent = 8;

		// False when a pool is exhausted, with whatever was created left for the caller to
		// release; the manager has already logged which pool refused.
		bool
		CreateLevelTarget(
			IResourceManager& resourceManager,
			uint32_t          width,
			uint32_t          height,
			std::string_view  kind,
			uint32_t          level,
			TextureHandle&    texture,
			RtvHandle&        rtv,
			SrvHandle&        srv)
		{
			auto textureDesc   = TextureDesc();
			textureDesc.width  = width;
			textureDesc.height = height;
			textureDesc.format = c_BloomFormat;
			textureDesc.usage =
				TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };
			textureDesc.initialLayout = BarrierLayout::kRenderTarget;
			textureDesc.debugName     = std::format("Bloom {}: {}", kind, level);

			texture = resourceManager.CreateTexture(textureDesc);
			if (texture.IsNull())
			{
				return false;
			}

			auto rtvDesc      = RtvDesc();
			rtvDesc.format    = c_BloomFormat;
			rtvDesc.debugName = std::format("Bloom {} RTV: {}", kind, level);

			auto srvDesc      = SrvDesc();
			srvDesc.format    = c_BloomFormat;
			srvDesc.debugName = std::format("Bloom {} SRV: {}", kind, level);

			rtv = resourceManager.CreateRtv(texture, rtvDesc);
			srv = resourceManager.CreateSrv(texture, srvDesc);

			return !rtv.IsNull() && !srv.IsNull();
		}
	}

	void
	BloomChain::Ensure(ResourceManagerRef resourceManager, uint32_t width, uint32_t height)
	{
		gassert(width > 0 && height > 0, "A bloom chain cannot be zero-sized");

		const bool sameSize = m_Width == width && m_Height == height;

		if (sameSize && (!m_Levels.empty() || m_AllocationFailed))
		{
			return;
		}

		Release();
		m_AllocationFailed = false;

		m_ResourceManager = std::move(resourceManager);
		m_Width           = width;
		m_Height          = height;

		bool created = true;

		for (uint32_t i = 0; created && i < c_MaxBloomLevels; ++i)
		{
			const uint32_t levelWidth  = std::max(1u, width >> (i + 1));
			const uint32_t levelHeight = std::max(1u, height >> (i + 1));

			if (!m_Levels.empty() && std::min(levelWidth, levelHeight) < c_MinBloomExtent)
			{
				break;
			}

			auto& level  = m_Levels.emplace_back();
			level.width  = levelWidth;
			level.height = levelHeight;

			created = CreateLevelTarget(
				*m_ResourceManager,
				levelWidth,
				levelHeight,
				"Down",
				i,
				level.downTexture,
				level.downRtv,
				level.downSrv);
		}

		// The last level is only ever read back, so it keeps its downsample alone.
		for (uint32_t i = 0; created && i + 1 < m_Levels.size(); ++i)
		{
			Level& level = m_Levels[i];

			created = CreateLevelTarget(
				*m_ResourceManager,
				level.width,
				level.height,
				"Up",
				i,
				level.upTexture,
				level.upRtv,
				level.upSrv);
		}

		// A pool ran dry: bloom is skipped rather than the frame lost. Not retried every frame --
		// each attempt makes the pool log its own refusal, so a per-frame retry is a per-frame
		// error. A resize or a Retry is what asks again.
		if (!created)
		{
			logger::error(
				"Bloom chain for {}x{} could not be allocated; bloom is skipped until the target "
				"resizes or bloom is re-enabled",
				width,
				height);

			Release();
			m_Width            = width;
			m_Height           = height;
			m_AllocationFailed = true;
		}
	}

	void
	BloomChain::Retry() noexcept
	{
		m_AllocationFailed = false;
		m_Width            = 0;
		m_Height           = 0;
	}

	void
	BloomChain::Release() noexcept
	{
		// Handle by handle, because a chain a pool refused mid-way is released too and holds
		// every shape of partially-created level.
		if (m_ResourceManager)
		{
			for (Level& level : m_Levels)
			{
				if (!level.downSrv.IsNull())
				{
					m_ResourceManager->DestroySrv(level.downSrv);
				}
				if (!level.downRtv.IsNull())
				{
					m_ResourceManager->DestroyRtv(level.downRtv);
				}
				if (!level.downTexture.IsNull())
				{
					m_ResourceManager->DestroyTexture(level.downTexture);
				}

				if (!level.upSrv.IsNull())
				{
					m_ResourceManager->DestroySrv(level.upSrv);
				}
				if (!level.upRtv.IsNull())
				{
					m_ResourceManager->DestroyRtv(level.upRtv);
				}
				if (!level.upTexture.IsNull())
				{
					m_ResourceManager->DestroyTexture(level.upTexture);
				}
			}
		}

		m_Levels.clear();
		m_Width  = 0;
		m_Height = 0;
	}
}
