#include <core/platform/memory.h>
#include <core/profiling/TaggedBytes.h>
#include <core/profiling/memory.h>

/**
 * The tag counters are process-wide and every case here shares them, so nothing asserts an absolute
 * total: each case measures the *delta* it caused. That is also how a caller reads them in anger --
 * a phase costs what it added, not what the process happens to hold.
 */

using namespace core::profiling;

namespace
{
	constexpr MemoryTag c_Tag   = MemoryTag::kMesh;
	constexpr MemoryTag c_Other = MemoryTag::kTexture;

	uint64_t
	LiveOf(const MemoryTag tag)
	{
		return tag_totals(tag).live;
	}

	uint64_t
	AllocationsOf(const MemoryTag tag)
	{
		return tag_totals(tag).allocations;
	}
}

TEST_CASE("A charge is released when its holder dies", "[memory]")
{
	const uint64_t before      = LiveOf(c_Tag);
	const uint64_t allocations = AllocationsOf(c_Tag);

	{
		const TaggedBytes charge(c_Tag, 4096);
		CHECK(LiveOf(c_Tag) == before + 4096);
		CHECK(AllocationsOf(c_Tag) == allocations + 1);
	}

	CHECK(LiveOf(c_Tag) == before);
	CHECK(AllocationsOf(c_Tag) == allocations);
}

TEST_CASE("A peak survives the release that follows it", "[memory]")
{
	reset_memory_peaks();

	const uint64_t base = LiveOf(c_Tag);
	{
		const TaggedBytes charge(c_Tag, 1024 * 1024);
	}

	// The whole point of a peak: the bytes are gone and the number the OS had to honour is not.
	CHECK(LiveOf(c_Tag) == base);
	CHECK(tag_totals(c_Tag).peak >= base + 1024 * 1024);
}

TEST_CASE("A moved charge is counted once", "[memory]")
{
	const uint64_t before      = LiveOf(c_Tag);
	const uint64_t allocations = AllocationsOf(c_Tag);

	{
		TaggedBytes       original(c_Tag, 2048);
		const TaggedBytes moved(std::move(original));

		// A move that charged again would read 4096 here, and a moved-from holder that still
		// released would take the live count below `before` when the pair goes out of scope.
		CHECK(LiveOf(c_Tag) == before + 2048);
		CHECK(AllocationsOf(c_Tag) == allocations + 1);
		CHECK(moved.Bytes() == 2048);
	}

	CHECK(LiveOf(c_Tag) == before);
	CHECK(AllocationsOf(c_Tag) == allocations);
}

TEST_CASE("Re-seating a charge releases the one it replaces", "[memory]")
{
	const uint64_t before = LiveOf(c_Tag);

	TaggedBytes charge(c_Tag, 512);
	charge = TaggedBytes(c_Tag, 8192);

	// The container grew; it did not acquire a second buffer.
	CHECK(LiveOf(c_Tag) == before + 8192);
}

TEST_CASE("A tag is not charged for bytes taken under another", "[memory]")
{
	const uint64_t mesh    = LiveOf(c_Tag);
	const uint64_t texture = LiveOf(c_Other);

	const TaggedBytes outer(c_Tag, 64);
	const TaggedBytes inner(c_Other, 128);

	CHECK(LiveOf(c_Tag) == mesh + 64);
	CHECK(LiveOf(c_Other) == texture + 128);
}

TEST_CASE("The total carries every tag", "[memory]")
{
	const uint64_t before = memory_totals().live;

	const TaggedBytes mesh(c_Tag, 16);
	const TaggedBytes texture(c_Other, 32);

	CHECK(memory_totals().live == before + 48);
}

TEST_CASE("A default-constructed holder charges nothing", "[memory]")
{
	const uint64_t before = memory_totals().live;

	{
		const TaggedBytes empty;
		CHECK(empty.Bytes() == 0);
		CHECK(memory_totals().live == before);
	}

	CHECK(memory_totals().live == before);
}

TEST_CASE("Every tag has a name of its own", "[memory]")
{
	std::set<std::string_view> names;
	for (std::size_t tag = 0; tag < c_MemoryTagCount; ++tag)
	{
		const std::string_view name = tag_name(static_cast<MemoryTag>(tag));
		CHECK_FALSE(name.empty());
		names.insert(name);
	}

	// A duplicate would silently merge two subsystems in the report, and in Tracy's pool view.
	CHECK(names.size() == c_MemoryTagCount);
}

TEST_CASE("The process footprint is readable, or is honestly absent", "[memory]")
{
	const core::ProcessMemory memory = core::process_memory();

	// PLATFORM_WEB is PRIVATE to core, so the compiler's own macros are what a test can branch on.
#if defined(__EMSCRIPTEN__)
	CHECK(memory.footprint == 0);
#else
	// No upper bound: this is whatever the OS charges the test binary, and a ceiling would be a
	// machine's number rather than a property. That it is non-zero is the whole contract -- a
	// residual computed against zero would report every tagged byte as unaccounted.
	CHECK(memory.footprint > 0);
	CHECK((memory.peak == 0 || memory.peak >= memory.footprint));
#endif
}
