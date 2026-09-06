
#include <algorithm>
#include <assetlib/pak.h>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <core/file/IFileSystem.h>
#include <core/file/LayeredFileSystem.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	// A scratch directory that cleans up after itself, like the reference suites' DataRoot.
	struct Scratch
	{
		fs::path path;

		explicit Scratch(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path);
		}
		~Scratch() { fs::remove_all(path); }

		[[nodiscard]] fs::path
		Archive() const
		{
			return path / "Data.bpak";
		}
	};

	std::vector<std::byte>
	Bytes(std::string_view text)
	{
		std::vector<std::byte> out(text.size());
		std::memcpy(out.data(), text.data(), text.size());
		return out;
	}

	std::string
	Text(std::span<const std::byte> bytes)
	{
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	std::vector<std::string>
	Sorted(std::vector<std::string> paths)
	{
		std::ranges::sort(paths);
		return paths;
	}

	// One entry per asset kind the packer will really meet, including a payload long enough for a
	// range to land inside rather than spanning the whole thing.
	void
	WriteSample(const fs::path& archive)
	{
		assetlib::PakWriter writer(archive);
		writer.Add("Authored/Materials/kirk.bmaterial", Bytes("material bytes"), { 14, 1000 });
		writer.Add(
			"Derived/Meshes/kirk.bmesh",
			Bytes("0123456789abcdef0123456789abcdef"),
			{ 32, 2000 });
		writer.Add("Derived/BakedTextures/kirk_orm.ktx2", Bytes("texture"), { 7, 3000 });
		writer.Add("Derived/BakedTextures/nested/deep.ktx2", Bytes("deep"), { 4, 4000 });
		writer.Finish();
	}
}

TEST_CASE("a bpak round-trips every entry", "[pak]")
{
	const Scratch scratch("pak_roundtrip");
	WriteSample(scratch.Archive());

	const assetlib::PakFile pak(scratch.Archive());

	CHECK(pak.IsReadOnly());
	CHECK(pak.Exists("Authored/Materials/kirk.bmaterial"));
	CHECK_FALSE(pak.Exists("Authored/Materials/nobody.bmaterial"));

	CHECK(Text(pak.Read("Authored/Materials/kirk.bmaterial")) == "material bytes");
	CHECK(Text(pak.Read("Derived/Meshes/kirk.bmesh")) == "0123456789abcdef0123456789abcdef");
	CHECK(Text(pak.Read("Derived/BakedTextures/kirk_orm.ktx2")) == "texture");
	CHECK(Text(pak.Read("Derived/BakedTextures/nested/deep.ktx2")) == "deep");

	SECTION("a range reads only its bytes, from inside the entry")
	{
		CHECK(Text(pak.ReadRange("Derived/Meshes/kirk.bmesh", 16, 4)) == "0123");
		CHECK(pak.ReadRange("Derived/Meshes/kirk.bmesh", 32, 0).empty());
	}

	SECTION("a range past an entry's end throws, and does not reach the next entry")
	{
		CHECK_THROWS_AS(
			pak.ReadRange("Derived/BakedTextures/kirk_orm.ktx2", 4, 8),
			std::runtime_error);
		CHECK_THROWS_AS(
			pak.ReadRange("Derived/BakedTextures/kirk_orm.ktx2", UINT64_MAX, 4),
			std::runtime_error);
	}

	SECTION("a path that is not in the archive throws rather than returning empty")
	{
		CHECK_THROWS_AS(pak.Read("Authored/Materials/nobody.bmaterial"), std::runtime_error);
		CHECK_THROWS_AS(
			pak.ReadRange("Authored/Materials/nobody.bmaterial", 0, 1),
			std::runtime_error);
	}
}

TEST_CASE("a bpak carries the stamp each entry was packed with", "[pak]")
{
	const Scratch scratch("pak_stamps");
	WriteSample(scratch.Archive());

	const assetlib::PakFile pak(scratch.Archive());

	// The whole point: drawsLoose and bakeIsStale must reach the same verdict against an archive
	// that they reached against the tree it was packed from.
	CHECK(pak.Stat("Authored/Materials/kirk.bmaterial") == core::file::FileStamp{ 14, 1000 });
	CHECK(pak.Stat("Derived/Meshes/kirk.bmesh") == core::file::FileStamp{ 32, 2000 });
	CHECK_FALSE(pak.Stat("Authored/Materials/nobody.bmaterial").has_value());
}

TEST_CASE("a bpak enumerates exactly what went in", "[pak]")
{
	const Scratch scratch("pak_enumerate");
	WriteSample(scratch.Archive());

	const assetlib::PakFile pak(scratch.Archive());

	CHECK(
		Sorted(pak.Enumerate()) ==
		std::vector<std::string>{ "Authored/Materials/kirk.bmaterial",
	                              "Derived/BakedTextures/kirk_orm.ktx2",
	                              "Derived/BakedTextures/nested/deep.ktx2",
	                              "Derived/Meshes/kirk.bmesh" });

	CHECK(
		Sorted(pak.Enumerate("Derived/BakedTextures")) ==
		std::vector<std::string>{ "Derived/BakedTextures/kirk_orm.ktx2",
	                              "Derived/BakedTextures/nested/deep.ktx2" });

	CHECK(pak.Enumerate("Nothing").empty());
}

/**
 * The writer sorts its table, so what it was told in which order stops mattering to a reader.
 *
 * It does *not* make the two files byte-identical, and this pins that too: payloads are streamed as
 * they arrive, so Add order is payload order and the offsets recorded for one path differ between
 * these two archives. Reproducible bytes are a property of a caller that adds in a fixed order --
 * see AssetStore::Pack and `Pack_test.cpp`.
 */
TEST_CASE("a bpak reads the same however its entries were added", "[pak]")
{
	const Scratch scratch("pak_deterministic");

	const fs::path first  = scratch.path / "first.bpak";
	const fs::path second = scratch.path / "second.bpak";

	{
		assetlib::PakWriter writer(first);
		writer.Add("b.bin", Bytes("bbb"), { 3, 2 });
		writer.Add("a.bin", Bytes("aaa"), { 3, 1 });
		writer.Finish();
	}
	{
		assetlib::PakWriter writer(second);
		writer.Add("a.bin", Bytes("aaa"), { 3, 1 });
		writer.Add("b.bin", Bytes("bbb"), { 3, 2 });
		writer.Finish();
	}

	const assetlib::PakFile firstPak(first);
	const assetlib::PakFile secondPak(second);

	CHECK(firstPak.Enumerate() == secondPak.Enumerate());
	CHECK(Text(firstPak.Read("a.bin")) == Text(secondPak.Read("a.bin")));
	CHECK(Text(firstPak.Read("b.bin")) == Text(secondPak.Read("b.bin")));

	CHECK(
		core::file::read_file_bytes(first.string()) !=
		core::file::read_file_bytes(second.string()));
}

TEST_CASE("PakWriter refuses what a reader could not key on", "[pak]")
{
	const Scratch scratch("pak_writer_refuses");

	assetlib::PakWriter writer(scratch.Archive());
	writer.Add("Authored/Materials/kirk.bmaterial", Bytes("x"), {});

	CHECK_THROWS_AS(
		writer.Add("Authored/Materials/kirk.bmaterial", Bytes("y"), {}),
		std::runtime_error);

	// Same asset, two spellings: normalizeRef makes them one, so this is the duplicate above.
	CHECK_THROWS_AS(
		writer.Add("Derived/../Authored/Materials/kirk.bmaterial", Bytes("y"), {}),
		std::runtime_error);

	CHECK_THROWS_AS(writer.Add("../outside.bin", Bytes("y"), {}), std::runtime_error);
	CHECK_THROWS_AS(writer.Add("", Bytes("y"), {}), std::runtime_error);
}

TEST_CASE("an abandoned PakWriter leaves the target alone", "[pak]")
{
	const Scratch scratch("pak_abandoned");

	WriteSample(scratch.Archive());
	const auto before = fs::file_size(scratch.Archive());

	{
		assetlib::PakWriter writer(scratch.Archive());
		writer.Add("only.bin", Bytes("something"), {});
		// No Finish: the destructor removes the temp.
	}

	CHECK(fs::file_size(scratch.Archive()) == before);

	// And nothing was left beside it.
	std::vector<std::string> leftovers;
	for (const auto& entry : fs::directory_iterator(scratch.path))
		if (entry.path().extension() == ".tmp")
			leftovers.push_back(entry.path().filename().string());

	CHECK(leftovers.empty());
}

TEST_CASE("a malformed bpak throws rather than reading out of bounds", "[pak]")
{
	const Scratch scratch("pak_malformed");
	WriteSample(scratch.Archive());

	const std::vector<std::byte> good = [&] {
		std::ifstream          in(scratch.Archive(), std::ios::binary | std::ios::ate);
		std::vector<std::byte> bytes(static_cast<size_t>(in.tellg()));
		in.seekg(0);
		in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return bytes;
	}();

	const auto writeAndOpen = [&](std::span<const std::byte> bytes) {
		const fs::path broken = scratch.path / "broken.bpak";
		std::ofstream  out(broken, std::ios::binary | std::ios::trunc);
		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		out.close();
		return assetlib::PakFile(broken);
	};

	SECTION("bad magic")
	{
		std::vector<std::byte> bad = good;
		bad[0]                     = std::byte{ 'X' };
		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	SECTION("an unsupported major version")
	{
		std::vector<std::byte> bad = good;
		bad[4]                     = std::byte{ 99 };
		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	SECTION("truncated")
	{
		const std::vector<std::byte> bad(
			good.begin(),
			good.begin() + static_cast<ptrdiff_t>(good.size() / 2));
		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	SECTION("shorter than a header")
	{
		const std::vector<std::byte> bad(good.begin(), good.begin() + 8);
		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	SECTION("an entry table offset past the end of the file")
	{
		std::vector<std::byte> bad = good;

		// Header::entryTableOffset is at byte 16; see the layout in pak_io.cpp.
		const uint64_t bogus = UINT64_MAX - 8;
		std::memcpy(bad.data() + 16, &bogus, sizeof(bogus));

		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	// The two fields that size an allocation. Both must be measured against the file before the
	// vector is constructed, or a corrupt header asks for hundreds of gigabytes and the failure
	// arrives as bad_alloc -- which is not what this constructor documents throwing, and not
	// something a caller opening an archive is handling.
	SECTION("an entry count larger than the file could hold")
	{
		std::vector<std::byte> bad = good;

		const uint32_t bogus = UINT32_MAX;
		std::memcpy(bad.data() + 12, &bogus, sizeof(bogus));

		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}

	SECTION("a string pool larger than the file could hold")
	{
		std::vector<std::byte> bad = good;

		// Header::stringPoolSize is at byte 32.
		const uint64_t bogus = UINT64_MAX - 4096;
		std::memcpy(bad.data() + 32, &bogus, sizeof(bogus));

		CHECK_THROWS_AS(writeAndOpen(bad), std::runtime_error);
	}
}

TEST_CASE("many threads read one mounted bpak at once", "[pak]")
{
	const Scratch scratch("pak_concurrent");

	// Distinct payloads, each long enough that a torn read would be obvious.
	constexpr int c_Entries = 16;
	{
		assetlib::PakWriter writer(scratch.Archive());
		for (int i = 0; i < c_Entries; ++i)
			writer.Add(
				std::format("e{}.bin", i),
				Bytes(std::string(64, static_cast<char>('a' + i))),
				{});
		writer.Finish();
	}

	const assetlib::PakFile pak(scratch.Archive());

	// The contract IFileSystem promises, and the test that fails the day a reader grows a shared
	// seek position: two decode threads pulling two textures out of one archive is the ordinary
	// case for the editor's thumbnail cache.
	std::vector<std::thread> threads;
	std::atomic<int>         mismatches = 0;

	for (int i = 0; i < c_Entries; ++i)
		threads.emplace_back([&pak, &mismatches, i] {
			const std::string expected(64, static_cast<char>('a' + i));

			for (int repeat = 0; repeat < 32; ++repeat)
			{
				if (Text(pak.Read(std::format("e{}.bin", i))) != expected)
					++mismatches;

				if (Text(pak.ReadRange(std::format("e{}.bin", i), 8, 16)) != expected.substr(8, 16))
					++mismatches;
			}
		});

	for (std::thread& thread : threads) thread.join();

	CHECK(mismatches == 0);
}

TEST_CASE("a bpak mounts under a loose overlay", "[pak]")
{
	const Scratch scratch("pak_mounted");
	WriteSample(scratch.Archive());

	const fs::path loose = scratch.path / "loose";
	fs::create_directories(loose / "Authored/Materials");
	std::ofstream(loose / "Authored/Materials" / "kirk.bmaterial", std::ios::binary) << "edited";

	core::file::LayeredFileSystem mounted;
	mounted.Mount(std::make_shared<core::file::LooseFileSystem>(loose));
	mounted.Mount(std::make_shared<assetlib::PakFile>(scratch.Archive()));

	// The editor's shape: an edit shadows its packed twin, everything else comes from the archive.
	CHECK(Text(mounted.Read("Authored/Materials/kirk.bmaterial")) == "edited");
	CHECK(Text(mounted.Read("Derived/Meshes/kirk.bmesh")) == "0123456789abcdef0123456789abcdef");

	CHECK_FALSE(mounted.IsPathReadOnly("Authored/Materials/kirk.bmaterial"));
	CHECK(mounted.IsPathReadOnly("Derived/Meshes/kirk.bmesh"));

	CHECK(Sorted(mounted.Enumerate()).size() == 4);
}
