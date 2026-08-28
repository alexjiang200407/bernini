#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "CheckedFileReader.h"
#include "cache_io.h"
#include <assetlib/asset_import.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Node.h>
#include <core/file/file.h>

using namespace assetlib;

namespace
{
	enum class Id : uint32_t
	{
		kA = 1,
		kB,
		kEmpty
	};

	std::vector<std::byte>
	SampleEntry(const SourceRef& source)
	{
		cache::Writer               writer;
		const std::vector<uint32_t> a = { 1, 2, 3 };
		const std::vector<uint8_t>  b = { 9 };
		writer.Add(Id::kA, a);
		writer.Add(Id::kB, b);
		writer.Add(Id::kEmpty, std::vector<uint64_t>());
		return writer.Finish(0xABCD1234u, 42, source);
	}

	SourceRef
	SampleSource()
	{
		SourceRef source;
		source.key            = "Authored/Meshes/kirk.glb";
		source.stamp          = { 128, 0xFEED };
		source.parametersHash = 7;
		return source;
	}
}

TEST_CASE("a cache entry round-trips its chunks and its key", "[cacheio]")
{
	const auto bytes = SampleEntry(SampleSource());

	const cache::Reader reader(bytes, 0xABCD1234u, 42, "test");
	CHECK(reader.GetSource() == SampleSource());
	CHECK(reader.Require<uint32_t>(Id::kA) == std::vector<uint32_t>{ 1, 2, 3 });
	CHECK(reader.Require<uint8_t>(Id::kB) == std::vector<uint8_t>{ 9 });

	// Empty but present: Require answers an empty vector -- a mesh with no roots chunk is a
	// mesh with no roots, not a malformed file.
	CHECK(reader.Require<uint64_t>(Id::kEmpty).empty());
	CHECK(reader.Read<float>(Id{ 99 }).empty());
}

TEST_CASE("a never-recorded source reads back empty", "[cacheio]")
{
	const auto          bytes = SampleEntry(SourceRef{});
	const cache::Reader reader(bytes, 0xABCD1234u, 42, "test");
	CHECK(reader.GetSource().key.empty());
	CHECK(reader.GetSource().stamp == SourceStamp{});
}

TEST_CASE("the key peeks without the payload, foreign token included", "[cacheio]")
{
	namespace fs      = std::filesystem;
	const fs::path at = fs::temp_directory_path() / "bernini_cache_peek.bin";
	core::file::write_atomic(at, SampleEntry(SampleSource()));

	CheckedFileReader reader(at, "test");
	const auto        key = cache::peekKey(reader, 0xABCD1234u, "test");
	CHECK(key.bakeToken == 42);
	CHECK(key.source == SampleSource());
	fs::remove(at);
}

TEST_CASE("a ranged read fetches the asked-for chunks and the key alone", "[cacheio]")
{
	namespace fs      = std::filesystem;
	const fs::path at = fs::temp_directory_path() / "bernini_cache_ranged.bin";
	core::file::write_atomic(at, SampleEntry(SampleSource()));

	const std::array<uint32_t, 1> ids = { { static_cast<uint32_t>(Id::kA) } };
	const cache::CacheData data = cache::readCacheChunksFromFile(at, 0xABCD1234u, 42, ids, "test");
	CHECK(data.key.source == SampleSource());
	CHECK(data.Read<uint32_t>(Id::kA, "test") == std::vector<uint32_t>{ 1, 2, 3 });
	CHECK(data.Read<uint8_t>(Id::kB, "test").empty());
	fs::remove(at);
}

TEST_CASE("applyBindings is a pure function of the document", "[cacheio][importdoc]")
{
	BMesh mesh;
	for (const char* name : { "hull", "sail", "flag" })
	{
		Submesh submesh{};
		submesh.nameOffset = mesh.stringPool.add(name);
		submesh.material   = 0;  // deliberately dirty: stale state must not leak through
		mesh.submeshes.push_back(submesh);
	}
	mesh.materials = { "Authored/Materials/stale.bmaterial" };

	const std::vector<MaterialBinding> bindings = {
		{ "flag", "Authored/Materials/cloth.bmaterial" },
		{ "hull", "Authored/Materials/wood.bmaterial" },
		{ "sail", "Authored/Materials/cloth.bmaterial" },
	};
	CHECK(applyBindings(mesh, bindings).empty());

	// First-appearance order over the submeshes, stale entries gone, shared materials shared.
	CHECK(
		mesh.materials == std::vector<std::string>{ "Authored/Materials/wood.bmaterial",
	                                                "Authored/Materials/cloth.bmaterial" });
	CHECK(mesh.submeshes[0].material == 0);
	CHECK(mesh.submeshes[1].material == 1);
	CHECK(mesh.submeshes[2].material == 1);

	SECTION("a second application of the same document is a no-op")
	{
		BMesh again = mesh;
		CHECK(applyBindings(again, bindings).empty());
		CHECK(again.materials == mesh.materials);
	}

	SECTION("an unbound submesh is unbound, not defaulted")
	{
		CHECK(applyBindings(
				  mesh,
				  std::vector<MaterialBinding>{ { "hull", "Authored/Materials/wood.bmaterial" } })
		          .empty());
		CHECK(mesh.submeshes[1].material == c_InvalidIndex);
	}

	SECTION("a binding whose submesh vanished is reported, never guessed at")
	{
		const std::vector<std::string> unbound = applyBindings(
			mesh,
			std::vector<MaterialBinding>{ { "anchor", "Authored/Materials/iron.bmaterial" },
		                                  { "hull", "Authored/Materials/wood.bmaterial" } });
		CHECK(unbound == std::vector<std::string>{ "anchor" });
		// The bindings that do match still land; the report is the caller's to escalate.
		CHECK(mesh.materials == std::vector<std::string>{ "Authored/Materials/wood.bmaterial" });
	}
}
