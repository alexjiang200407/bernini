#include "gfx/SceneColorCopy.h"
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <core/err/util.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <utility>

namespace bgl
{
	namespace
	{
		// Scene colour's own format: the copy carries the same exposed linear radiance.
		constexpr bgpu::Format c_CopyFormat = bgpu::Format::RGBA16_FLOAT;
	}

	void
	SceneColorCopy::Ensure(
		bgpu::ResourceManagerRef resourceManager,
		uint32_t                 width,
		uint32_t                 height)
	{
		core::ensure(width > 0 && height > 0, "A scene colour copy cannot be zero-sized");

		if (m_Width == width && m_Height == height && (!m_Texture.IsNull() || m_AllocationFailed))
		{
			return;
		}

		Release();
		m_AllocationFailed = false;

		m_ResourceManager = std::move(resourceManager);
		m_Width           = width;
		m_Height          = height;

		auto textureDesc          = bgpu::TextureDesc();
		textureDesc.width         = width;
		textureDesc.height        = height;
		textureDesc.format        = c_CopyFormat;
		textureDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
			                                            bgpu::TextureUsageFlag::kSRV };
		textureDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
		textureDesc.debugName     = "Scene Colour Copy";

		m_Texture = m_ResourceManager->CreateTexture(textureDesc);
		if (!m_Texture.IsNull())
		{
			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_CopyFormat;
			rtvDesc.debugName = "Scene Colour Copy RTV";

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_CopyFormat;
			srvDesc.debugName = "Scene Colour Copy SRV";

			m_Rtv = m_ResourceManager->CreateRtv(m_Texture, rtvDesc);
			m_Srv = m_ResourceManager->CreateSrv(m_Texture, srvDesc);
		}

		// A pool ran dry: the frame's water is skipped rather than the frame lost, and not retried
		// every frame -- each attempt makes the pool log its own refusal.
		if (m_Texture.IsNull() || m_Rtv.IsNull() || m_Srv.IsNull())
		{
			spdlog::error(
				"Scene colour copy for {}x{} could not be allocated; water is not drawn until the "
				"target resizes",
				width,
				height);

			Release();
			m_Width            = width;
			m_Height           = height;
			m_AllocationFailed = true;
		}
	}

	void
	SceneColorCopy::Release() noexcept
	{
		if (m_ResourceManager)
		{
			if (!m_Srv.IsNull())
			{
				m_ResourceManager->DestroySrv(m_Srv);
			}
			if (!m_Rtv.IsNull())
			{
				m_ResourceManager->DestroyRtv(m_Rtv);
			}
			if (!m_Texture.IsNull())
			{
				m_ResourceManager->DestroyTexture(m_Texture);
			}
		}

		m_Texture = bgpu::TextureHandle();
		m_Rtv     = bgpu::RtvHandle();
		m_Srv     = bgpu::SrvHandle();
		m_Width   = 0;
		m_Height  = 0;
	}
}
