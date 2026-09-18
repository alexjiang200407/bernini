#pragma once

#include <concepts>
#include <core/process_api.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
namespace core::profiling
{
	/**
	 * A tag enum this facility can charge bytes to.
	 *
	 * The enum itself belongs to whoever has subsystems to name — `core` is shared by every target
	 * and has no business knowing what a mesh is. What it asks of one is a count and a name per
	 * value, both found by ADL beside the enum:
	 *
	 * ```cpp
	 * enum class MyTag : uint8_t { kThing, kOther, kCount };
	 *
	 * constexpr std::size_t MemoryTagCount(MyTag)          { return 2; }
	 * constexpr const char* MemoryTagName(const MyTag tag) { return tag == MyTag::kThing ? … ; }
	 * ```
	 *
	 * The name must be a literal with static lifetime: it is stored, and Tracy keeps the pointer
	 * rather than the string.
	 */
	template <typename Tag>
	concept MemoryTagEnum = std::is_enum_v<Tag> && requires(Tag tag) {
		{ MemoryTagCount(tag) } -> std::convertible_to<std::size_t>;
		{ MemoryTagName(tag) } -> std::convertible_to<const char*>;
	};

	struct MemoryTotals
	{
		/** Bytes charged and not yet released. */
		uint64_t live;

		/** The largest `live` ever reached, which survives the release that follows it. */
		uint64_t peak;

		/** Tracked allocations currently live, so an average size is `live / allocations`. */
		uint64_t allocations;
	};

	/** One tag's line in a report: the name its enum gave it, and what it holds. */
	struct MemoryTagTotals
	{
		std::string_view name;
		MemoryTotals     totals;
	};

	namespace detail
	{
		/**
		 * The type-erased half, so a report never names a tag enum.
		 *
		 * A charge is templated and a report is not: the editor and `assetlib_cli` both write one
		 * while naming no tag at all, and templating the report would drag whichever enum they are
		 * built against into both of them. So each `TaggedBytes<Tag>` registers its table the first
		 * time it is used, and a report walks what has registered.
		 */
		class TagTable
		{
		public:
			CORE_PROCESS_API
			TagTable(std::size_t count, std::string_view (*nameOf)(std::size_t)) noexcept;

			CORE_PROCESS_API void
			Charge(std::size_t tag, uint64_t bytes) noexcept;

			CORE_PROCESS_API void
			Discharge(std::size_t tag, uint64_t bytes) noexcept;

			[[nodiscard]] CORE_PROCESS_API MemoryTotals
			Totals(std::size_t tag) const noexcept;

			[[nodiscard]] CORE_PROCESS_API std::size_t
										   Count() const noexcept;

			[[nodiscard]] CORE_PROCESS_API std::string_view
										   NameOf(std::size_t tag) const noexcept;

			CORE_PROCESS_API void
			ResetPeaks() noexcept;

		private:
			struct Impl;
			std::shared_ptr<Impl> m_Impl;
		};

		/** Registers `table` so a report can find it, and hands back the same one every time. */
		[[nodiscard]] CORE_PROCESS_API TagTable&
		register_table(
			std::string_view key,
			std::size_t      count,
			std::string_view (*nameOf)(std::size_t));

		/** One id an allocation is keyed on, never an address — see TaggedBytes. */
		[[nodiscard]] CORE_PROCESS_API uint64_t
		mint_allocation_id() noexcept;

		CORE_PROCESS_API void
		tracy_alloc(uint64_t id, uint64_t bytes, const char* pool) noexcept;

		CORE_PROCESS_API void
		tracy_free(uint64_t id, const char* pool) noexcept;

		/** The one table `Tag` charges, made on first use rather than during static init. */
		template <MemoryTagEnum Tag>
		TagTable&
		table_for() noexcept
		{
			static TagTable& g_Table = register_table(
				typeid(Tag).name(),
				static_cast<std::size_t>(MemoryTagCount(Tag{})),
				[](const std::size_t tag) -> std::string_view {
					return MemoryTagName(static_cast<Tag>(tag));
				});
			return g_Table;
		}
	}

	/** What `tag` holds now. */
	template <MemoryTagEnum Tag>
	[[nodiscard]] MemoryTotals
	tag_totals(const Tag tag) noexcept
	{
		return detail::table_for<Tag>().Totals(static_cast<std::size_t>(tag));
	}

	/**
	 * Every tag of every registered enum summed, whose `peak` is the high-water of the *total* and
	 * so is not the sum of the per-tag peaks — two subsystems that peak at different moments never
	 * cost their sum.
	 */
	[[nodiscard]] CORE_PROCESS_API MemoryTotals
	memory_totals() noexcept;

	/** Every registered tag, for a report. Order is registration order, then enum order. */
	[[nodiscard]] CORE_PROCESS_API std::vector<MemoryTagTotals>
								   memory_tag_totals();

	/**
	 * Drops every peak to its tag's current `live`, so the next reading measures one phase.
	 *
	 * Does not touch `live`, which belongs to allocations that are still charged: a reset that
	 * cleared it would make the next release underflow.
	 */
	CORE_PROCESS_API void
	reset_memory_peaks() noexcept;
}
