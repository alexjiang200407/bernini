#pragma once

namespace assetlib
{
	/**
	 * The presentation knobs a chunk-era `.bsky` carried before they moved to the `.benv`
	 * document. Read by `migrate`'s one-time lift alone -- the `.benv` must take them before the
	 * `.bsky` re-saves without them -- and deleted with the schema system.
	 */
	struct SkyPresentation
	{
		uint32_t mipLevel  = 0;
		float    rotationY = 0.0f;
	};

	/** @throws what the legacy chunk reader throws on anything but a chunk-era `.bsky`. */
	[[nodiscard]] SkyPresentation
	legacySkyPresentation(std::span<const std::byte> bytes);

	/**
	 * The authored exposure a chunk-era `.benvl` carried before it moved to the `.benv` document.
	 * Nullopt when nobody ever authored one. The same one-time lift, and the same lifetime.
	 */
	[[nodiscard]] std::optional<float>
	legacyLightingExposureOverride(std::span<const std::byte> bytes);
}
