#include <core/file/file.h>
#include <core/hash.h>

namespace
{
	// A temp file holding `bytes`, removed when the fixture goes out of scope. Named per test so
	// two cases in the same run cannot collide on it.
	struct TempFile
	{
		std::filesystem::path path;

		TempFile(const char* name, const std::vector<std::byte>& bytes) :
			path(std::filesystem::temp_directory_path() / name)
		{
			std::ofstream out(path, std::ios::binary);
			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}

		~TempFile()
		{
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	};

	std::vector<std::byte>
	Pattern(size_t size)
	{
		std::vector<std::byte> bytes(size);
		for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::byte>((i * 31 + 7) & 0xFF);
		return bytes;
	}
}

// The chunking is the whole implementation, so what has to hold is that it is invisible: a file
// read 64 KiB at a time must hash to what the same bytes hash to in one call. The sizes straddle
// the chunk boundary, where an off-by-one would hide.
TEST_CASE("Hashing a file in chunks equals hashing its bytes at once", "[hash]")
{
	constexpr size_t c_Chunk = 64 * 1024;

	for (const size_t size :
	     { size_t{ 0 }, size_t{ 1 }, c_Chunk - 1, c_Chunk, c_Chunk + 1, 3 * c_Chunk + 17 })
	{
		INFO("size " << size);

		const auto     bytes = Pattern(size);
		const TempFile file("core_hash_file_chunks.bin", bytes);

		const auto hashed = core::file::hash_file(file.path);

		REQUIRE(hashed.has_value());
		REQUIRE(*hashed == core::hash_bytes(bytes.data(), bytes.size(), core::hash_seed()));
	}
}

// The bug this exists for: a checkout rewrites mtimes without touching content, and a stamp keyed
// on the hash must not notice.
TEST_CASE("A file's hash does not move when only its mtime does", "[hash]")
{
	const TempFile file("core_hash_file_mtime.bin", Pattern(4096));

	const auto before = core::file::hash_file(file.path);

	std::filesystem::last_write_time(
		file.path,
		std::filesystem::last_write_time(file.path) + std::chrono::seconds(5));

	const auto after = core::file::hash_file(file.path);

	REQUIRE(before.has_value());
	REQUIRE(after == before);
}

TEST_CASE("Two files differing in one byte hash differently", "[hash]")
{
	auto       bytes = Pattern(1024);
	const auto left  = TempFile("core_hash_file_left.bin", bytes);
	bytes[512]       = static_cast<std::byte>(std::to_integer<uint8_t>(bytes[512]) ^ 1u);
	const auto right = TempFile("core_hash_file_right.bin", bytes);

	REQUIRE(core::file::hash_file(left.path) != core::file::hash_file(right.path));
}

TEST_CASE("A file that cannot be opened has no hash", "[hash]")
{
	REQUIRE(
		core::file::hash_file(std::filesystem::temp_directory_path() / "core_hash_absent.bin") ==
		std::nullopt);
}
