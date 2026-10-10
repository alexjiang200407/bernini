#include "scene/HzbChain.h"
#include <bgl/idl/Constants.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <core/err/util.h>
#include <cstdint>
#include <format>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr bgpu::Format c_HzbFormat = bgpu::Format::R32_FLOAT;

		// False when a pool is exhausted, with whatever was created left for the caller to
		// release; the manager has already logged which pool refused.
		bool
		CreateLevel(bgpu::IResourceManager& resourceManager, HzbChain::Level& level, uint32_t index)
		{
			auto textureDesc          = bgpu::TextureDesc();
			textureDesc.width         = level.width;
			textureDesc.height        = level.height;
			textureDesc.format        = c_HzbFormat;
			textureDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                            bgpu::TextureUsageFlag::kSRV };
			textureDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
			textureDesc.debugName     = std::format("HZB {}", index);

			level.texture = resourceManager.CreateTexture(textureDesc);
			if (level.texture.IsNull())
			{
				return false;
			}

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_HzbFormat;
			rtvDesc.debugName = std::format("HZB RTV {}", index);

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_HzbFormat;
			srvDesc.debugName = std::format("HZB SRV {}", index);

			level.rtv = resourceManager.CreateRtv(level.texture, rtvDesc);
			level.srv = resourceManager.CreateSrv(level.texture, srvDesc);

			return !level.rtv.IsNull() && !level.srv.IsNull();
		}
	}

	HzbChain::HzbChain(HzbChain&& other) noexcept :
		m_ResourceManager(std::move(other.m_ResourceManager)), m_Levels(std::move(other.m_Levels)),
		m_Width(other.m_Width), m_Height(other.m_Height), m_Valid(other.m_Valid),
		m_AllocationFailed(other.m_AllocationFailed)
	{
		other.m_Levels.clear();
		other.m_Width  = 0;
		other.m_Height = 0;
		other.m_Valid  = false;
	}

	HzbChain&
	HzbChain::operator=(HzbChain&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			m_ResourceManager  = std::move(other.m_ResourceManager);
			m_Levels           = std::move(other.m_Levels);
			m_Width            = other.m_Width;
			m_Height           = other.m_Height;
			m_Valid            = other.m_Valid;
			m_AllocationFailed = other.m_AllocationFailed;
			other.m_Levels.clear();
			other.m_Width  = 0;
			other.m_Height = 0;
			other.m_Valid  = false;
		}
		return *this;
	}

	bool
	HzbChain::Ensure(bgpu::ResourceManagerRef resourceManager, uint32_t width, uint32_t height)
	{
		core::ensure(width > 0 && height > 0, "An HZB cannot be zero-sized");

		const bool sameSize = m_Width == width && m_Height == height;
		if (sameSize && (!m_Levels.empty() || m_AllocationFailed))
		{
			return false;
		}

		Release();
		m_AllocationFailed = false;
		m_ResourceManager  = std::move(resourceManager);
		m_Width            = width;
		m_Height           = height;

		bool     created     = true;
		uint32_t levelWidth  = width;
		uint32_t levelHeight = height;
		for (uint32_t i = 0; created && i < idl::cMaxHzbLevels; ++i)
		{
			levelWidth  = (levelWidth + 1u) / 2u;
			levelHeight = (levelHeight + 1u) / 2u;

			auto& level  = m_Levels.emplace_back();
			level.width  = levelWidth;
			level.height = levelHeight;
			created      = CreateLevel(*m_ResourceManager, level, i);

			if (levelWidth == 1u && levelHeight == 1u)
			{
				break;
			}
		}

		// A pool ran dry: the frustum culls by its planes alone rather than losing the frame. Not
		// retried every frame -- each attempt makes the pool log its own refusal.
		if (!created)
		{
			spdlog::error(
				"HZB for a {}x{} depth could not be allocated; occlusion culling is skipped until "
				"the target resizes",
				width,
				height);
			Release();
			m_Width            = width;
			m_Height           = height;
			m_AllocationFailed = true;
		}
		return true;
	}

	void
	HzbChain::Release() noexcept
	{
		// Handle by handle, because a chain a pool refused mid-way is released too and holds a
		// partially-created level.
		if (m_ResourceManager)
		{
			for (Level& level : m_Levels)
			{
				if (!level.srv.IsNull())
				{
					m_ResourceManager->DestroySrv(level.srv);
				}
				if (!level.rtv.IsNull())
				{
					m_ResourceManager->DestroyRtv(level.rtv);
				}
				if (!level.texture.IsNull())
				{
					m_ResourceManager->DestroyTexture(level.texture);
				}
			}
		}

		m_Levels.clear();
		m_Width  = 0;
		m_Height = 0;
		m_Valid  = false;
	}

	std::string
	HzbLevelName(const uint32_t level)
	{
		return std::format("cull.hzb{}", level);
	}
}
