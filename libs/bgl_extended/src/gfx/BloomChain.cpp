#include "gfx/BloomChain.h"
#include "types/Format.h"
#include <algorithm>
#include <bgl_common/gassert.h>
#include <cstdint>
#include <format>
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
	}

	void
	BloomChain::Ensure(ResourceManagerRef resourceManager, uint32_t width, uint32_t height)
	{
		gassert(width > 0 && height > 0, "A bloom chain cannot be zero-sized");

		if (!m_Levels.empty() && m_Width == width && m_Height == height)
		{
			return;
		}

		Release();

		m_ResourceManager = std::move(resourceManager);
		m_Width           = width;
		m_Height          = height;

		for (uint32_t i = 0; i < c_MaxBloomLevels; ++i)
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

			auto textureDesc   = TextureDesc();
			textureDesc.width  = levelWidth;
			textureDesc.height = levelHeight;
			textureDesc.format = c_BloomFormat;
			textureDesc.usage =
				TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };
			textureDesc.initialLayout = BarrierLayout::kRenderTarget;

			auto rtvDesc   = RtvDesc();
			rtvDesc.format = c_BloomFormat;

			auto srvDesc   = SrvDesc();
			srvDesc.format = c_BloomFormat;

			textureDesc.debugName = std::format("Bloom Down: {}", i);
			rtvDesc.debugName     = std::format("Bloom Down RTV: {}", i);
			srvDesc.debugName     = std::format("Bloom Down SRV: {}", i);

			level.downTexture = m_ResourceManager->CreateTexture(textureDesc);
			level.downRtv     = m_ResourceManager->CreateRtv(level.downTexture, rtvDesc);
			level.downSrv     = m_ResourceManager->CreateSrv(level.downTexture, srvDesc);
		}

		// The last level is only ever read back, so it keeps its downsample alone.
		for (uint32_t i = 0; i + 1 < m_Levels.size(); ++i)
		{
			Level& level = m_Levels[i];

			auto textureDesc   = TextureDesc();
			textureDesc.width  = level.width;
			textureDesc.height = level.height;
			textureDesc.format = c_BloomFormat;
			textureDesc.usage =
				TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };
			textureDesc.initialLayout = BarrierLayout::kRenderTarget;
			textureDesc.debugName     = std::format("Bloom Up: {}", i);

			auto rtvDesc      = RtvDesc();
			rtvDesc.format    = c_BloomFormat;
			rtvDesc.debugName = std::format("Bloom Up RTV: {}", i);

			auto srvDesc      = SrvDesc();
			srvDesc.format    = c_BloomFormat;
			srvDesc.debugName = std::format("Bloom Up SRV: {}", i);

			level.upTexture = m_ResourceManager->CreateTexture(textureDesc);
			level.upRtv     = m_ResourceManager->CreateRtv(level.upTexture, rtvDesc);
			level.upSrv     = m_ResourceManager->CreateSrv(level.upTexture, srvDesc);
		}
	}

	void
	BloomChain::Release() noexcept
	{
		if (m_ResourceManager)
		{
			for (Level& level : m_Levels)
			{
				m_ResourceManager->DestroySrv(level.downSrv);
				m_ResourceManager->DestroyRtv(level.downRtv);
				m_ResourceManager->DestroyTexture(level.downTexture);

				if (!level.upTexture.IsNull())
				{
					m_ResourceManager->DestroySrv(level.upSrv);
					m_ResourceManager->DestroyRtv(level.upRtv);
					m_ResourceManager->DestroyTexture(level.upTexture);
				}
			}
		}

		m_Levels.clear();
		m_Width  = 0;
		m_Height = 0;
	}
}
