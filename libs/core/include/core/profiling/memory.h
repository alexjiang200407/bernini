#pragma once

namespace core::profiling
{
	/**
	 * The subsystem a tracked allocation is charged to.
	 *
	 * Deliberately coarse: one label per thing somebody can act on, at the granularity of Unreal's
	 * `ELLMTag`. A finer taxonomy is a set of labels nobody maintains, and an unmaintained tag
	 * reports a number nobody trusts. What no tag covers is not lost -- it shows up as the residual
	 * against `core::process_memory`, which is the signal that a tag is missing.
	 */
	enum class MemoryTag : uint8_t
	{
		kMesh,
		kAnimation,
		kTexture,
		kMaterial,
		kEnvironment,
		kShader,
		kDeviceBuffer,
		kDeviceTexture,
		kEditor,
		kCount
	};

	inline constexpr std::size_t c_MemoryTagCount = static_cast<std::size_t>(MemoryTag::kCount);

	/** A stable, lower-case name, safe to store: every one is a literal with static lifetime. */
	[[nodiscard]] std::string_view
	tag_name(MemoryTag tag) noexcept;

	struct MemoryTotals
	{
		/** Bytes charged and not yet released. */
		uint64_t live;

		/** The largest `live` ever reached, which survives the release that follows it. */
		uint64_t peak;

		/** Tracked allocations currently live, so an average size is `live / allocations`. */
		uint64_t allocations;
	};

	[[nodiscard]] MemoryTotals
	tag_totals(MemoryTag tag) noexcept;

	/**
	 * Every tag summed, whose `peak` is the high-water of the *total* and so is not the sum of the
	 * per-tag peaks -- two subsystems that peak at different moments never cost their sum.
	 */
	[[nodiscard]] MemoryTotals
	memory_totals() noexcept;

	/**
	 * Drops every peak to the tag's current `live`, so the next reading measures one phase.
	 *
	 * Does not touch `live`, which belongs to allocations that are still charged: a reset that
	 * cleared it would make the next release underflow.
	 */
	void
	reset_memory_peaks() noexcept;
}
