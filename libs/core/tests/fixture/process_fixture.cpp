#include <core/log/log.h>
#include <core/profiling/TaggedBytes.h>
#include <core/profiling/memory.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#if defined(_WIN32)
#	define FIXTURE_API extern "C" __declspec(dllexport)
#else
#	define FIXTURE_API extern "C" __attribute__((visibility("default")))
#endif

/**
 * A library that links core the way a renderer or a plugin does, loaded by core_tests at runtime
 * so it binds to whatever copy of core its own link gave it. See Process_test.cpp.
 */

namespace
{
	enum class FixtureTag : uint8_t
	{
		kHeld,
		kCount
	};

	constexpr std::size_t
	MemoryTagCount(FixtureTag) noexcept
	{
		return static_cast<std::size_t>(FixtureTag::kCount);
	}

	constexpr const char*
	MemoryTagName(FixtureTag) noexcept
	{
		return "process fixture";
	}

	std::optional<core::profiling::TaggedBytes<FixtureTag>> g_Held;
}

FIXTURE_API void
CoreFixtureHold(const uint64_t bytes)
{
	g_Held.emplace(FixtureTag::kHeld, bytes);
}

FIXTURE_API void
CoreFixtureRelease()
{
	g_Held.reset();
}

FIXTURE_API uint64_t
CoreFixtureMintId()
{
	return core::profiling::detail::mint_allocation_id();
}

FIXTURE_API const void*
CoreFixtureDefaultLogger()
{
	return spdlog::default_logger_raw();
}

FIXTURE_API void
CoreFixtureInitFileLogger(const char* const fileName)
{
	core::logging::init_file_logger(fileName, spdlog::level::info);
}
