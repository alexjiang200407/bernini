#pragma once
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bgl
{
	/**
	 * The hierarchical depth ladder one culled frustum tests its occludees against: one
	 * `R32_FLOAT` texture per level, level 0 half the depth buffer's size and each level half the
	 * one above, rounded up, down to one texel or cMaxHzbLevels. Every texel holds the farthest
	 * depth of the texels it covers -- the minimum, under reversed-Z -- so a box whose nearest point
	 * is farther than that is hidden wherever it lands.
	 *
	 * Separate textures rather than one mip chain: the frame graph tracks a texture whole, and a
	 * level is read while the next is drawn. Rounding up keeps every source texel under some
	 * destination texel, which is what a dropped last row or column of an odd level would break.
	 *
	 * Sized by Ensure at the first draw that culls by occlusion and again after a resize, which
	 * also makes it invalid: Valid says the levels hold the depth of this frustum's previous draw,
	 * which only a build (MarkBuilt) makes true.
	 */
	class HzbChain
	{
	public:
		struct Level
		{
			bgpu::TextureHandle texture;
			bgpu::RtvHandle     rtv;
			bgpu::SrvHandle     srv;
			uint32_t            width  = 0;
			uint32_t            height = 0;
		};

		HzbChain() = default;
		~HzbChain() noexcept { Release(); }

		HzbChain(const HzbChain&) noexcept = delete;
		HzbChain(HzbChain&& other) noexcept;

		HzbChain&
		operator=(const HzbChain&) noexcept = delete;

		HzbChain&
		operator=(HzbChain&& other) noexcept;

		/**
		 * Creates the ladder for a depth buffer of `width` x `height`, or nothing when it already
		 * fits. A size change releases the old ladder (deferred, so frames in flight keep what they
		 * recorded) and builds the new one, invalid until its first build.
		 *
		 * @return whether the ladder was (re)made.
		 * @post the levels are empty when a resource pool was exhausted; the caller culls by the
		 *       frustum alone. A failed size is not asked again until the size changes.
		 */
		bool
		Ensure(bgpu::ResourceManagerRef resourceManager, uint32_t width, uint32_t height);

		/** Deferred-destroys every level. Safe with frames in flight, and idempotent. */
		void
		Release() noexcept;

		/** Finest first; empty until Ensure has run, or when a pool refused the size. */
		[[nodiscard]] std::span<const Level>
		GetLevels() const noexcept
		{
			return m_Levels;
		}

		/** The depth buffer size the ladder was made for. */
		[[nodiscard]] uint32_t
		GetDepthWidth() const noexcept
		{
			return m_Width;
		}

		[[nodiscard]] uint32_t
		GetDepthHeight() const noexcept
		{
			return m_Height;
		}

		/** Whether the levels hold the depth of the frustum's previous draw at this size. */
		[[nodiscard]] bool
		IsValid() const noexcept
		{
			return m_Valid;
		}

		/** A build was attached this frame: the next draw may test against the ladder. */
		void
		MarkBuilt() noexcept
		{
			m_Valid = !m_Levels.empty();
		}

		/** No build this frame: the next draw tests nothing against the ladder. */
		void
		Invalidate() noexcept
		{
			m_Valid = false;
		}

	private:
		bgpu::ResourceManagerRef m_ResourceManager;
		std::vector<Level>       m_Levels;
		uint32_t                 m_Width  = 0;
		uint32_t                 m_Height = 0;
		bool                     m_Valid  = false;

		// Set when the pools refused this size, so Ensure does not ask them again every frame.
		bool m_AllocationFailed = false;
	};

	/** The frame-graph name a frustum's `level` is imported under, inside the frustum's scope. */
	[[nodiscard]] std::string
	HzbLevelName(uint32_t level);
}
