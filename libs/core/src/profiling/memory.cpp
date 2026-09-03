#include <core/profiling/TaggedBytes.h>
#include <core/profiling/memory.h>

#include <tracy/Tracy.hpp>

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
			// makes each of those an error. A counter is a fixed cell in the table and is only ever
			// read through a reference, so deleting them says what was already true.
			Counter() noexcept      = default;
			Counter(const Counter&) = delete;
			Counter(Counter&&)      = delete;
			Counter&
			operator=(const Counter&) = delete;
			Counter&
			operator=(Counter&&) = delete;
		};

		// Function-local rather than namespace-scope: a charge taken during dynamic initialisation
		// would otherwise reach a table not yet constructed, and a release during teardown one
		// already destroyed.
		Counter&
		counter_for(const MemoryTag tag) noexcept
		{
			static std::array<Counter, c_MemoryTagCount + 1> g_Counters;
			return g_Counters[static_cast<std::size_t>(tag)];
		}

		// The whole-table high-water, which is not the sum of the per-tag peaks: two subsystems
		// peaking at different moments never cost their sum. Indexed one past the tags.
		Counter&
		total_counter() noexcept
		{
			return counter_for(MemoryTag::kCount);
		}

		void
		raise_peak(Counter& counter, const uint64_t live) noexcept
		{
			auto peak = counter.peak.load(std::memory_order_relaxed);
			while (peak < live &&
			       !counter.peak.compare_exchange_weak(peak, live, std::memory_order_relaxed))
			{}
		}

		void
		charge(Counter& counter, const uint64_t bytes) noexcept
		{
			const auto live = counter.live.fetch_add(bytes, std::memory_order_relaxed) + bytes;
			counter.allocations.fetch_add(1, std::memory_order_relaxed);
			raise_peak(counter, live);
		}

		void
		discharge(Counter& counter, const uint64_t bytes) noexcept
		{
			counter.live.fetch_sub(bytes, std::memory_order_relaxed);
			counter.allocations.fetch_sub(1, std::memory_order_relaxed);
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

		// Tracy stores the pool name pointer rather than the string, so it must outlive the client.
		const char*
		pool_name(const MemoryTag tag) noexcept
		{
			switch (tag)
			{
			case MemoryTag::kMesh:
				return "mesh";
			case MemoryTag::kAnimation:
				return "animation";
			case MemoryTag::kTexture:
				return "texture";
			case MemoryTag::kMaterial:
				return "material";
			case MemoryTag::kEnvironment:
				return "environment";
			case MemoryTag::kShader:
				return "shader";
			case MemoryTag::kDeviceBuffer:
				return "device buffer";
			case MemoryTag::kDeviceTexture:
				return "device texture";
			case MemoryTag::kEditor:
				return "editor";
			case MemoryTag::kCount:
				break;
			}
			return "unknown";
		}
	}

	std::string_view
	tag_name(const MemoryTag tag) noexcept
	{
		return pool_name(tag);
	}

	MemoryTotals
	tag_totals(const MemoryTag tag) noexcept
	{
		return read_totals(counter_for(tag));
	}

	MemoryTotals
	memory_totals() noexcept
	{
		return read_totals(total_counter());
	}

	void
	reset_memory_peaks() noexcept
	{
		for (std::size_t tag = 0; tag <= c_MemoryTagCount; ++tag)
		{
			Counter& counter = counter_for(static_cast<MemoryTag>(tag));
			counter.peak.store(
				counter.live.load(std::memory_order_relaxed),
				std::memory_order_relaxed);
		}
	}

	TaggedBytes::TaggedBytes(const MemoryTag tag, const std::size_t bytes) noexcept :
		m_Tag(tag), m_Bytes(bytes)
	{
		static std::atomic<uint64_t> g_NextId{ 1 };
		m_Id = g_NextId.fetch_add(1, std::memory_order_relaxed);

		charge(counter_for(tag), bytes);
		charge(total_counter(), bytes);

#ifdef TRACY_ENABLE
		TracyAllocN(reinterpret_cast<void*>(m_Id), bytes, pool_name(tag));
#endif
	}

	TaggedBytes::TaggedBytes(TaggedBytes&& other) noexcept :
		m_Tag(other.m_Tag), m_Bytes(other.m_Bytes), m_Id(other.m_Id)
	{
		other.m_Id = 0;
	}

	TaggedBytes&
	TaggedBytes::operator=(TaggedBytes&& other) noexcept
	{
		if (this == &other)
			return *this;

		Release();

		m_Tag      = other.m_Tag;
		m_Bytes    = other.m_Bytes;
		m_Id       = other.m_Id;
		other.m_Id = 0;

		return *this;
	}

	TaggedBytes::~TaggedBytes() noexcept { Release(); }

	void
	TaggedBytes::Release() noexcept
	{
		if (m_Id == 0)
			return;

#ifdef TRACY_ENABLE
		TracyFreeN(reinterpret_cast<void*>(m_Id), pool_name(m_Tag));
#endif

		discharge(counter_for(m_Tag), m_Bytes);
		discharge(total_counter(), m_Bytes);

		m_Id = 0;
	}
}
