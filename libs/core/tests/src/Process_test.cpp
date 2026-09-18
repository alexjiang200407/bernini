#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>
#include <core/log/log.h>
#include <core/profiling/memory.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#	include <windows.h>
#else
#	include <dlfcn.h>
#endif

/**
 * core is linked into every binary of a process, and what it keeps for the process must still exist
 * once. The fixture links core the way the renderer and a plugin do and is loaded at runtime, which
 * is the case that binds it to its own copy of anything core has not put in `core_process`.
 */

using namespace core::profiling;

namespace
{
	class Fixture
	{
	public:
		/** Loaded once and never unloaded: the registry keeps the fixture's tag names by pointer. */
		static const Fixture&
		Get()
		{
			static const Fixture c_Fixture;
			return c_Fixture;
		}

		template <typename Fn>
		[[nodiscard]] Fn*
		Find(const char* const name) const
		{
#if defined(_WIN32)
			auto* const symbol =
				reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_Handle), name));
#else
			auto* const symbol = dlsym(m_Handle, name);
#endif
			if (symbol == nullptr)
				throw std::runtime_error(std::string("process fixture exports no ") + name);
			return reinterpret_cast<Fn*>(symbol);
		}

	private:
		Fixture()
		{
#if defined(_WIN32)
			// CMake spells the path with '/', which LoadLibrary does not accept.
			m_Handle =
				LoadLibraryW(std::filesystem::path(CORE_PROCESS_FIXTURE).make_preferred().c_str());
#else
			m_Handle = dlopen(CORE_PROCESS_FIXTURE, RTLD_NOW | RTLD_LOCAL);
#endif
			if (m_Handle == nullptr)
				throw std::runtime_error("cannot load " CORE_PROCESS_FIXTURE);
		}

		void* m_Handle = nullptr;
	};

	uint64_t
	LiveNamed(const std::string_view name)
	{
		uint64_t live = 0;
		for (const MemoryTagTotals& tag : memory_tag_totals())
		{
			if (tag.name == name)
				live += tag.totals.live;
		}
		return live;
	}
}

TEST_CASE("A charge made in a loaded library reaches this binary's report", "[process]")
{
	const Fixture& fixture = Fixture::Get();
	const uint64_t before  = memory_totals().live;

	fixture.Find<void(uint64_t)>("CoreFixtureHold")(4096);

	CHECK(memory_totals().live == before + 4096);
	CHECK(LiveNamed("process fixture") == 4096);

	fixture.Find<void()>("CoreFixtureRelease")();

	CHECK(memory_totals().live == before);
}

TEST_CASE("A loaded library mints allocation ids from this binary's sequence", "[process]")
{
	const Fixture& fixture = Fixture::Get();

	const uint64_t first  = detail::mint_allocation_id();
	const uint64_t middle = fixture.Find<uint64_t()>("CoreFixtureMintId")();
	const uint64_t last   = detail::mint_allocation_id();

	CHECK(first < middle);
	CHECK(middle < last);
}

TEST_CASE("A loaded library logs through this binary's default logger", "[process]")
{
	const Fixture& fixture = Fixture::Get();

	CHECK(
		fixture.Find<const void*()>("CoreFixtureDefaultLogger")() == spdlog::default_logger_raw());
}

TEST_CASE("A loaded library's init_file_logger opens no second log", "[process]")
{
	const Fixture&                        fixture  = Fixture::Get();
	const std::shared_ptr<spdlog::logger> previous = spdlog::default_logger();
	const std::filesystem::path directory = core::file::get_executable_path().parent_path();

	core::logging::init_file_logger("core_tests_process.log", spdlog::level::info);
	fixture.Find<void(const char*)>("CoreFixtureInitFileLogger")("core_process_fixture.log");

	const bool second = std::filesystem::exists(directory / "core_process_fixture.log");

	// Dropping the installed logger closes its file, so both can be removed.
	spdlog::set_default_logger(previous);
	std::error_code ec;
	std::filesystem::remove(directory / "core_tests_process.log", ec);
	std::filesystem::remove(directory / "core_process_fixture.log", ec);

	CHECK_FALSE(second);
}
