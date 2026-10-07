#include <bgpu/GpuContext.h>
#include <bgpu/ProgramCache.h>
#include <catch2/catch_test_macros.hpp>
#include <core/platform/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	constexpr auto c_Renderer = bgpu::ProgramCacheOwner{ .tag = "renderer", .formatVersion = 1 };
	constexpr auto c_Compute  = bgpu::ProgramCacheOwner{ .tag = "compute", .formatVersion = 1 };

	using ModuleEntries = std::vector<bgpu::ProgramEntryPoint>;

	const ModuleEntries c_Program = {
		{ "programs.forward.Opaque", "vsMain" },
		{ "programs.forward.Opaque", "psMain" },
	};

	// A directory of this case's own, wiped on the way in and out: `just test` shards a suite across
	// processes, and each gets its own temp root, but a pid keeps two runs of one case apart too.
	struct ScratchDir
	{
		fs::path path;

		explicit ScratchDir(std::string_view name) :
			path(
				fs::temp_directory_path() /
				std::format("bernini_programcache_{}_{}", name, core::process_id()))
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}

		~ScratchDir()
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}

		ScratchDir(const ScratchDir&) = delete;
		ScratchDir&
		operator=(const ScratchDir&) = delete;
	};

	bgpu::GpuContextRef
	ContextCachingIn(const fs::path& cacheDir, const fs::path& clientDir = {})
	{
		auto desc            = bgpu::GpuContextDesc();
		desc.shaderCacheDir  = cacheDir;
		desc.clientShaderDir = clientDir;
		return bgpu::CreateGpuContext(desc);
	}

	std::vector<std::byte>
	Bytes(std::string_view text)
	{
		const auto view = std::as_bytes(std::span<const char>(text.data(), text.size()));
		return std::vector<std::byte>(view.begin(), view.end());
	}

	void
	WriteText(const fs::path& path, std::string_view text)
	{
		std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
	}

	// The loop every owner writes: key what is about to be compiled, load it, and compile and store
	// only on a miss.
	std::vector<std::byte>
	LoadOrCompile(
		const bgpu::ProgramCache&                      cache,
		const bgpu::ProgramCacheOwner&                 owner,
		const ModuleEntries&                           moduleEntries,
		const std::function<std::vector<std::byte>()>& compile)
	{
		const uint64_t         key = cache.ComputeKey(owner, moduleEntries);
		std::vector<std::byte> bytes;
		if (cache.TryLoadProgram(key, bytes))
			return bytes;

		bytes = compile();
		cache.StoreProgram(key, bytes);
		return bytes;
	}
}

TEST_CASE("A context with no cache directory has no program cache", "[device][shadercache]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	CHECK(context->GetProgramCache() == nullptr);
}

TEST_CASE("A program compiles on a miss and loads on every later hit", "[device][shadercache]")
{
	const auto scratch = ScratchDir("hit");
	auto       context = ContextCachingIn(scratch.path);

	const bgpu::ProgramCache* cache = context->GetProgramCache();
	REQUIRE(cache != nullptr);
	CHECK(cache->GetDirectory() == scratch.path);
	CHECK(fs::is_directory(scratch.path));

	int  compiles = 0;
	auto compile  = [&] {
		++compiles;
		return Bytes("DXIL or MSL, and reflection");
	};

	CHECK(
		LoadOrCompile(*cache, c_Renderer, c_Program, compile) ==
		Bytes("DXIL or MSL, and reflection"));
	CHECK(compiles == 1);

	CHECK(
		LoadOrCompile(*cache, c_Renderer, c_Program, compile) ==
		Bytes("DXIL or MSL, and reflection"));
	CHECK(compiles == 1);

	std::vector<std::byte> unused;
	CHECK_FALSE(
		cache->TryLoadProgram(cache->ComputeKey(c_Renderer, { { "never", "stored" } }), unused));
}

// What stops two owners on one context misreading each other: an entry is filed under its owner.
TEST_CASE("Two owners compiling the same program never share a key", "[device][shadercache]")
{
	const auto scratch = ScratchDir("owners");
	auto       context = ContextCachingIn(scratch.path);

	const bgpu::ProgramCache& cache = *context->GetProgramCache();

	const uint64_t key = cache.ComputeKey(c_Renderer, c_Program);

	CHECK(cache.ComputeKey(c_Compute, c_Program) != key);
	CHECK(cache.ComputeKey({ .tag = c_Renderer.tag, .formatVersion = 2 }, c_Program) != key);

	SECTION("the pairs are a set: their order is not part of the key")
	{
		const ModuleEntries reversed(c_Program.rbegin(), c_Program.rend());
		CHECK(cache.ComputeKey(c_Renderer, reversed) == key);
	}

	SECTION("an entry one owner stored is a miss for the other")
	{
		cache.StoreProgram(key, Bytes("the renderer's layout"));

		std::vector<std::byte> bytes;
		CHECK(cache.TryLoadProgram(key, bytes));
		CHECK_FALSE(cache.TryLoadProgram(cache.ComputeKey(c_Compute, c_Program), bytes));
	}
}

TEST_CASE("A source module registered on the context moves every key", "[device][shadercache]")
{
	const auto scratch = ScratchDir("module");
	auto       context = ContextCachingIn(scratch.path);

	const bgpu::ProgramCache& cache  = *context->GetProgramCache();
	const uint64_t            before = cache.ComputeKey(c_Renderer, c_Program);

	context->AddSourceModule(
		{ .name = "game.slot0", .source = "public float Slot() { return 0.0f; }" });
	const uint64_t registered = cache.ComputeKey(c_Renderer, c_Program);
	CHECK(registered != before);

	context->AddSourceModule(
		{ .name = "game.slot0", .source = "public float Slot() { return 0.0f; }" });
	CHECK(cache.ComputeKey(c_Renderer, c_Program) == registered);

	context->AddSourceModule(
		{ .name = "game.slot0", .source = "public float Slot() { return 1.0f; }" });
	CHECK(cache.ComputeKey(c_Renderer, c_Program) != registered);
}

// A key is only worth storing if the next run computes it again, and only safe if an edited source
// does not: both are a second context over the same files.
TEST_CASE(
	"A key is the same next run, until a file under the search paths changes",
	"[device][shadercache]")
{
	const auto scratch = ScratchDir("files");
	const auto client  = ScratchDir("files_client");
	fs::create_directories(client.path);
	WriteText(client.path / "Client.slang", "public float Client() { return 0.0f; }");

	uint64_t first = 0;
	{
		auto context = ContextCachingIn(scratch.path, client.path);
		first        = context->GetProgramCache()->ComputeKey(c_Renderer, c_Program);
	}

	{
		auto context = ContextCachingIn(scratch.path, client.path);
		CHECK(context->GetProgramCache()->ComputeKey(c_Renderer, c_Program) == first);
	}

	WriteText(client.path / "Client.slang", "public float Client() { return 1.0f; }");
	{
		auto context = ContextCachingIn(scratch.path, client.path);
		CHECK(context->GetProgramCache()->ComputeKey(c_Renderer, c_Program) != first);
	}
}

// The store checks its own files, so no owner decodes bytes it did not write.
TEST_CASE("A torn, altered or misplaced entry is a miss", "[device][shadercache]")
{
	const auto scratch = ScratchDir("torn");
	auto       context = ContextCachingIn(scratch.path);

	const bgpu::ProgramCache& cache = *context->GetProgramCache();

	const uint64_t key   = cache.ComputeKey(c_Renderer, c_Program);
	const uint64_t other = cache.ComputeKey(c_Compute, c_Program);
	const fs::path entry = scratch.path / std::format("{:016x}.bsc", key);

	cache.StoreProgram(key, Bytes("a program the size of a few words"));
	REQUIRE(fs::exists(entry));

	std::vector<std::byte> bytes;
	REQUIRE(cache.TryLoadProgram(key, bytes));

	SECTION("truncated")
	{
		fs::resize_file(entry, fs::file_size(entry) - 3);
		CHECK_FALSE(cache.TryLoadProgram(key, bytes));
	}

	SECTION("a payload byte changed")
	{
		auto file = std::fstream(entry, std::ios::binary | std::ios::in | std::ios::out);
		file.seekp(-1, std::ios::end);
		file.put('!');
		file.close();
		CHECK_FALSE(cache.TryLoadProgram(key, bytes));
	}

	SECTION("garbage, as a pre-header entry would read")
	{
		WriteText(entry, "garbage");
		CHECK_FALSE(cache.TryLoadProgram(key, bytes));
	}

	SECTION("filed under another key")
	{
		fs::copy_file(entry, scratch.path / std::format("{:016x}.bsc", other));
		CHECK_FALSE(cache.TryLoadProgram(other, bytes));
	}

	SECTION("stored again, it loads again")
	{
		WriteText(entry, "garbage");
		cache.StoreProgram(key, Bytes("a program the size of a few words"));
		CHECK(cache.TryLoadProgram(key, bytes));
		CHECK(bytes == Bytes("a program the size of a few words"));
	}
}
