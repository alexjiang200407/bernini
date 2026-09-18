#pragma once
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include <cstdint>
#include <span>
#include <vector>

namespace bgl
{
	/**
	 * The per-target ladder the bloom passes render through: one downsample and one upsample
	 * texture per level, each half the extent of the one above, starting at half the output size.
	 *
	 * Owned beside the target's other attachments but created here, through the resource manager,
	 * because nothing about it is backend state. Sized lazily by Ensure at the first frame that
	 * blooms, and re-sized the same way after a resize -- the old ladder is deferred-destroyed, so
	 * frames in flight keep what they recorded.
	 */
	class BloomChain
	{
	public:
		struct Level
		{
			TextureHandle downTexture;
			RtvHandle     downRtv;
			SrvHandle     downSrv;

			// Null on the last level, which is never upsampled into.
			TextureHandle upTexture;
			RtvHandle     upRtv;
			SrvHandle     upSrv;

			uint32_t width  = 0;
			uint32_t height = 0;
		};

		BloomChain() = default;
		~BloomChain() noexcept { Release(); }

		BloomChain(const BloomChain&) noexcept = delete;
		BloomChain(BloomChain&&) noexcept      = delete;

		BloomChain&
		operator=(const BloomChain&) noexcept = delete;

		BloomChain&
		operator=(BloomChain&&) noexcept = delete;

		/**
		 * Creates the ladder for an output of `width` x `height`, or nothing when it already fits.
		 * The first call records the resource manager; a size change releases the old ladder
		 * (deferred) and builds the new one.
		 */
		void
		Ensure(ResourceManagerRef resourceManager, uint32_t width, uint32_t height);

		/** Deferred-destroys every level. Safe with frames in flight, and idempotent. */
		void
		Release() noexcept;

		[[nodiscard]] std::span<const Level>
		GetLevels() const noexcept
		{
			return m_Levels;
		}

		/**
		 * Whether the combine's input is level 0's upsample. False on an output too small to carry
		 * a second level, where the single downsample stands alone -- the one rule deciding which
		 * texture, which SRV and which frame-graph name the combine reads.
		 */
		[[nodiscard]] bool
		IsUpsampled() const noexcept
		{
			return m_Levels.size() > 1;
		}

		/** The finished bloom the combine samples. Null until Ensure has run. */
		[[nodiscard]] SrvHandle
		GetBloomSrv() const noexcept
		{
			if (m_Levels.empty())
			{
				return SrvHandle();
			}

			return IsUpsampled() ? m_Levels.front().upSrv : m_Levels.front().downSrv;
		}

	private:
		ResourceManagerRef m_ResourceManager;
		std::vector<Level> m_Levels;
		uint32_t           m_Width  = 0;
		uint32_t           m_Height = 0;
	};
}
