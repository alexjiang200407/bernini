#pragma once
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <cstdint>

namespace bgl
{
	/**
	 * The copy of scene colour a water surface refracts through: what the opaque phases drew, taken
	 * just before Forward Water, which draws into scene colour and so cannot sample it. Sized at the
	 * first frame that draws water, as BloomChain is at the first that blooms.
	 */
	class SceneColorCopy
	{
	public:
		SceneColorCopy() = default;
		~SceneColorCopy() noexcept { Release(); }

		SceneColorCopy(const SceneColorCopy&) noexcept = delete;
		SceneColorCopy(SceneColorCopy&&) noexcept      = delete;

		SceneColorCopy&
		operator=(const SceneColorCopy&) noexcept = delete;

		SceneColorCopy&
		operator=(SceneColorCopy&&) noexcept = delete;

		/**
		 * Creates the copy for a scene colour of `width` x `height`, or nothing when it already fits.
		 *
		 * @post the handles are null when a resource pool was exhausted -- the caller draws no water
		 *       rather than refracting through a null view. A failed size is not asked again until
		 *       the size changes.
		 */
		void
		Ensure(bgpu::ResourceManagerRef resourceManager, uint32_t width, uint32_t height);

		/** Deferred-destroys the copy. Safe with frames in flight, and idempotent. */
		void
		Release() noexcept;

		[[nodiscard]] bgpu::TextureHandle
		GetTexture() const noexcept
		{
			return m_Texture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetRtv() const noexcept
		{
			return m_Rtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetSrv() const noexcept
		{
			return m_Srv;
		}

	private:
		bgpu::ResourceManagerRef m_ResourceManager;
		bgpu::TextureHandle      m_Texture;
		bgpu::RtvHandle          m_Rtv;
		bgpu::SrvHandle          m_Srv;
		uint32_t                 m_Width  = 0;
		uint32_t                 m_Height = 0;

		// Set when the pools refused this size, so Ensure does not ask them again every frame.
		bool m_AllocationFailed = false;
	};
}
