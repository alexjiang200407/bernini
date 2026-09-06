#include <atomic>
#include <core/profiling/memory.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace core::profiling
{
	namespace
	{
		struct Counter
		{
			std::atomic<uint64_t> live{ 0 };
			std::atomic<uint64_t> peak{ 0 };
			std::atomic<uint64_t> allocations{ 0 };

			// Spelled out because an atomic member deletes all four implicitly, and MSVC's /Wall
			// makes each of those an error. A counter is a fixed cell in a table and is only ever
			// reached through a reference, so deleting them says what was already true.
			Counter() noexcept      = default;
			Counter(const Counter&) = delete;
			Counter(Counter&&)      = delete;
			Counter&
			operator=(const Counter&) = delete;
			Counter&
			operator=(Counter&&) = delete;
		};

		void
		raise_peak(Counter& counter, const uint64_t live) noexcept
		{
			auto peak = counter.peak.load(std::memory_order_relaxed);
			while (peak < live &&
			       !counter.peak.compare_exchange_weak(peak, live, std::memory_order_relaxed))
			{}
		}

		MemoryTotals
		read_totals(const Counter& counter) noexcept
		{
			return MemoryTotals{
				.live        = counter.live.load(std::memory_order_relaxed),
				.peak        = counter.peak.load(std::memory_order_relaxed),
				.allocations = counter.allocations.load(std::memory_order_relaxed),
			};
		}

		// The whole-process high-water, which is not the sum of the per-tag peaks: two subsystems
		// peaking at different moments never cost their sum.
		Counter&
		grand_total() noexcept
		{
			static Counter g_Total;
			return g_Total;
		}

		/**
		 * Every table that has been used, in first-use order so a report reads the same way twice.
		 *
		 * Function-local: a table registers itself the first time something charges to it, which
		 * may be during another translation unit's dynamic initialisation.
		 */
		struct Registry
		{
			std::mutex                                            lock;
			std::vector<std::pair<std::string, detail::TagTable>> tables;

			// A mutex member deletes all four implicitly, which MSVC's /Wall makes an error. The
			// registry is a function-local singleton reached by reference, so deleting them says
			// what was already true.
			Registry()                = default;
			Registry(const Registry&) = delete;
			Registry(Registry&&)      = delete;
			Registry&
			operator=(const Registry&) = delete;
			Registry&
			operator=(Registry&&) = delete;
		};

		Registry&
		registry() noexcept
		{
			static Registry g_Registry;
			return g_Registry;
		}
	}

	namespace detail
	{
		struct TagTable::Impl
		{
			std::vector<Counter> counters;
			std::string_view (*nameOf)(std::size_t);

			Impl(const std::size_t count, std::string_view (*names)(std::size_t)) :
				counters(count), nameOf(names)
			{}

			// Held by one shared_ptr and never copied, and its counters could not be copied anyway.
			Impl(const Impl&) = delete;
			Impl(Impl&&)      = delete;
			Impl&
			operator=(const Impl&) = delete;
			Impl&
			operator=(Impl&&) = delete;
		};

		TagTable::TagTable(
			const std::size_t count,
			std::string_view (*nameOf)(std::size_t)) noexcept :
			m_Impl(std::make_shared<Impl>(count, nameOf))
		{}

		void
		TagTable::Charge(const std::size_t tag, const uint64_t bytes) noexcept
		{
			if (tag >= m_Impl->counters.size())
				return;

			Counter&   counter = m_Impl->counters[tag];
			const auto live    = counter.live.fetch_add(bytes, std::memory_order_relaxed) + bytes;
			counter.allocations.fetch_add(1, std::memory_order_relaxed);
			raise_peak(counter, live);

			Counter&   total     = grand_total();
			const auto totalLive = total.live.fetch_add(bytes, std::memory_order_relaxed) + bytes;
			total.allocations.fetch_add(1, std::memory_order_relaxed);
			raise_peak(total, totalLive);
		}

		void
		TagTable::Discharge(const std::size_t tag, const uint64_t bytes) noexcept
		{
			if (tag >= m_Impl->counters.size())
				return;

			Counter& counter = m_Impl->counters[tag];
			counter.live.fetch_sub(bytes, std::memory_order_relaxed);
			counter.allocations.fetch_sub(1, std::memory_order_relaxed);

			Counter& total = grand_total();
			total.live.fetch_sub(bytes, std::memory_order_relaxed);
			total.allocations.fetch_sub(1, std::memory_order_relaxed);
		}

		MemoryTotals
		TagTable::Totals(const std::size_t tag) const noexcept
		{
			return tag < m_Impl->counters.size() ? read_totals(m_Impl->counters[tag]) :
			                                       MemoryTotals{};
		}

		std::size_t
		TagTable::Count() const noexcept
		{
			return m_Impl->counters.size();
		}

		std::string_view
		TagTable::NameOf(const std::size_t tag) const noexcept
		{
			return tag < m_Impl->counters.size() ? m_Impl->nameOf(tag) : std::string_view();
		}

		void
		TagTable::ResetPeaks() noexcept
		{
			for (Counter& counter : m_Impl->counters)
			{
				counter.peak.store(
					counter.live.load(std::memory_order_relaxed),
					std::memory_order_relaxed);
			}
		}

		TagTable&
		register_table(
			const std::string_view key,
			const std::size_t      count,
			std::string_view (*nameOf)(std::size_t))
		{
			Registry&                         all = registry();
			const std::lock_guard<std::mutex> held(all.lock);

			for (auto& [registered, table] : all.tables)
			{
				if (registered == key)
					return table;
			}

			all.tables.emplace_back(std::string(key), TagTable(count, nameOf));
			return all.tables.back().second;
		}

		uint64_t
		mint_allocation_id() noexcept
		{
			static std::atomic<uint64_t> g_NextId{ 1 };
			return g_NextId.fetch_add(1, std::memory_order_relaxed);
		}

		void
		tracy_alloc(
			[[maybe_unused]] const uint64_t id,
			[[maybe_unused]] const uint64_t bytes,
			[[maybe_unused]] const char*    pool) noexcept
		{
#ifdef TRACY_ENABLE
			TracyAllocN(reinterpret_cast<void*>(id), bytes, pool);
#endif
		}

		void
		tracy_free([[maybe_unused]] const uint64_t id, [[maybe_unused]] const char* pool) noexcept
		{
#ifdef TRACY_ENABLE
			TracyFreeN(reinterpret_cast<void*>(id), pool);
#endif
		}
	}

	MemoryTotals
	memory_totals() noexcept
	{
		return read_totals(grand_total());
	}

	std::vector<MemoryTagTotals>
	memory_tag_totals()
	{
		Registry&                         all = registry();
		const std::lock_guard<std::mutex> held(all.lock);

		auto totals = std::vector<MemoryTagTotals>();
		for (const auto& [key, table] : all.tables)
		{
			for (std::size_t tag = 0; tag < table.Count(); ++tag)
				totals.emplace_back(MemoryTagTotals{ table.NameOf(tag), table.Totals(tag) });
		}
		return totals;
	}

	void
	reset_memory_peaks() noexcept
	{
		Registry&                         all = registry();
		const std::lock_guard<std::mutex> held(all.lock);

		for (auto& [key, table] : all.tables) table.ResetPeaks();

		Counter& total = grand_total();
		total.peak.store(total.live.load(std::memory_order_relaxed), std::memory_order_relaxed);
	}
}
