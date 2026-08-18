#include "chunk_io.h"

#include <assetlib_structs/magic.h>
#include <schema/LayoutBuilder.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

/*
 * A container the way two branches would leave it. Master had Stamp{size, mtime}. Branch A added
 * Thing.weight and wrote its files as version 10; branch B changed the stamp to {size, hash} --
 * a change of meaning, renamed as the rule requires -- and also wrote version 10. The engine after
 * both merged is version 11 and wants {size, hash} plus weight. Neither branch's files can be told
 * apart by their number; the schema each carries is what tells them apart.
 */
namespace
{
	constexpr uint32_t c_Magic = 0x54534554;  // "TEST"

	struct StampOld
	{
		uint64_t size;
		uint64_t mtime;
	};

	struct StampNew
	{
		uint64_t size;
		uint64_t hash;
	};

	struct ThingMaster
	{
		uint32_t id;
		uint32_t pad;
		StampOld stamp;
	};

	struct ThingA
	{
		uint32_t id;
		float    weight;
		StampOld stamp;
	};

	struct ThingB
	{
		uint32_t id;
		uint32_t pad;
		StampNew stamp;
	};

	struct Thing
	{
		uint32_t id;
		float    weight;
		StampNew stamp;
	};

	enum class ChunkId : uint32_t
	{
		kThings = 1,
		kIndices,
		kNames
	};

	schema::Schema
	MasterSchema()
	{
		schema::Schema s;
		schema::LayoutBuilder<StampOld>(s, "Stamp")
			.AddField("size", &StampOld::size)
			.AddField("mtime", &StampOld::mtime)
			.Finish();
		schema::LayoutBuilder<ThingMaster>(s, "Thing")
			.AddField("id", &ThingMaster::id)
			.AddField("pad", &ThingMaster::pad)
			.AddField("stamp", &ThingMaster::stamp)
			.Finish();
		return s;
	}

	schema::Schema
	SchemaA()
	{
		schema::Schema s;
		schema::LayoutBuilder<StampOld>(s, "Stamp")
			.AddField("size", &StampOld::size)
			.AddField("mtime", &StampOld::mtime)
			.Finish();
		schema::LayoutBuilder<ThingA>(s, "Thing")
			.AddField("id", &ThingA::id)
			.AddField("weight", &ThingA::weight)
			.AddField("stamp", &ThingA::stamp)
			.Finish();
		return s;
	}

	schema::Schema
	SchemaB()
	{
		schema::Schema s;
		schema::LayoutBuilder<StampNew>(s, "Stamp")
			.AddField("size", &StampNew::size)
			.AddField("hash", &StampNew::hash)
			.Finish();
		schema::LayoutBuilder<ThingB>(s, "Thing")
			.AddField("id", &ThingB::id)
			.AddField("pad", &ThingB::pad)
			.AddField("stamp", &ThingB::stamp)
			.Finish();
		return s;
	}

	const schema::Schema&
	CurrentSchema()
	{
		static const schema::Schema c_Schema = [] {
			schema::Schema s;
			schema::LayoutBuilder<StampNew>(s, "Stamp")
				.AddField("size", &StampNew::size)
				.AddField("hash", &StampNew::hash)
				.Finish();
			schema::LayoutBuilder<Thing>(s, "Thing")
				.AddField("id", &Thing::id)
				.AddField("weight", &Thing::weight, 1.0f)
				.AddField("stamp", &Thing::stamp)
				.Finish();
			return s;
		}();
		return c_Schema;
	}

	struct Loaded
	{
		std::vector<Thing>       things;
		std::vector<uint32_t>    indices;
		std::vector<std::string> names;
		bool                     hookRan = false;
	};

	// The meaning hook: a file that still stamps by mtime gets a hash derived from it, so the
	// value is recovered rather than zeroed. Its predicate is the file's schema, never its version.
	const std::vector<chunk::Hook<Loaded>>&
	Hooks()
	{
		static const std::vector<chunk::Hook<Loaded>> c_Hooks = { chunk::Hook<Loaded>{
			.applies =
				[](const schema::Schema& stored) {
					const schema::Layout* stamp = stored.Find("Stamp");
					return stamp != nullptr &&
			               std::ranges::find(stamp->fields, "mtime", &schema::Field::name) !=
			                   stamp->fields.end();
				},
			.run =
				[](const chunk::Reader& reader, Loaded& loaded) {
					const schema::ElementView things = reader.View(ChunkId::kThings);
					for (size_t i = 0; i < things.GetCount(); ++i)
						loaded.things[i].stamp.hash =
							things.GetStruct("stamp", i).Get<uint64_t>("mtime") * 1000;
					loaded.hookRan = true;
				} } };
		return c_Hooks;
	}

	template <typename T>
	std::vector<std::byte>
	Write(const schema::Schema& schema, const std::vector<T>& things, uint16_t version)
	{
		chunk::Writer writer(schema);
		writer.Add(ChunkId::kThings, things);
		writer.Add(ChunkId::kIndices, std::vector<uint16_t>{ 1, 2, 65535 });
		writer.Add(ChunkId::kNames, chunk::packStrings(std::vector<std::string>{ "a", "b" }));
		return writer.Finish(c_Magic, version, 0);
	}

	Loaded
	Load(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, c_Magic, 11, "test", CurrentSchema());
		Loaded              loaded;
		loaded.things  = reader.Read<Thing>(ChunkId::kThings);
		loaded.indices = reader.Read<uint32_t>(ChunkId::kIndices);  // written as u16
		loaded.names   = chunk::unpackStrings(reader.Read<char>(ChunkId::kNames));
		chunk::applyHooks<Loaded>(Hooks(), reader, loaded);
		return loaded;
	}
}

TEST_CASE(
	"two files that both claim one version, with different shapes, both read",
	"[chunk][schema]")
{
	const auto fileA = Write(SchemaA(), std::vector<ThingA>{ { 7, 2.5f, { 100, 5 } } }, 10);
	const auto fileB = Write(SchemaB(), std::vector<ThingB>{ { 8, 0, { 200, 9999 } } }, 10);

	const Loaded a = Load(fileA);
	REQUIRE(a.things.size() == 1);
	CHECK(a.things[0].id == 7);
	CHECK(a.things[0].weight == 2.5f);
	CHECK(a.things[0].stamp.size == 100);
	CHECK(a.things[0].stamp.hash == 5000);  // recovered by the hook, from mtime
	CHECK(a.hookRan);

	const Loaded b = Load(fileB);
	REQUIRE(b.things.size() == 1);
	CHECK(b.things[0].id == 8);
	CHECK(b.things[0].weight == 1.0f);  // the default: B never had weight
	CHECK(b.things[0].stamp.hash == 9999);
	CHECK_FALSE(b.hookRan);  // B's schema says hash; the hook does not apply

	SECTION("value chunks widen and string chunks copy on both")
	{
		CHECK(a.indices == std::vector<uint32_t>{ 1, 2, 65535 });
		CHECK(b.names == std::vector<std::string>{ "a", "b" });
	}
}

TEST_CASE("a file from before both branches reads too, and takes every default", "[chunk][schema]")
{
	const auto   file = Write(MasterSchema(), std::vector<ThingMaster>{ { 1, 0, { 10, 3 } } }, 9);
	const Loaded m    = Load(file);
	REQUIRE(m.things.size() == 1);
	CHECK(m.things[0].weight == 1.0f);
	CHECK(m.things[0].stamp.hash == 3000);
	CHECK(m.hookRan);
}

TEST_CASE("a file the engine wrote itself is a copy, and the hook stays quiet", "[chunk][schema]")
{
	const auto   file = Write(CurrentSchema(), std::vector<Thing>{ { 3, 0.5f, { 1, 2 } } }, 11);
	const Loaded c    = Load(file);
	CHECK(c.things[0].stamp.hash == 2);
	CHECK_FALSE(c.hookRan);
}

TEST_CASE("the version decides one thing: a newer file is refused", "[chunk][schema]")
{
	const auto file = Write(CurrentSchema(), std::vector<Thing>{ { 3, 0.5f, { 1, 2 } } }, 12);
	REQUIRE_THROWS_WITH(
		Load(file),
		ContainsSubstring(
			"test: written by a newer engine (format 12; this build reads up to 11)"));
}

TEST_CASE("a file with no schema chunk is refused with the reason", "[chunk][schema]")
{
	// Every chunk but the schema's, under the old 24-byte table entry -- the shape a file had
	// before this container carried its schema.
	core::io::ByteWriter bytes;
	bytes.WritePod(chunk::Header{});
	bytes.AlignTo(chunk::c_Align);
	const auto    tableOffset = bytes.Size();
	chunk::Header header{};
	header.magic            = c_Magic;
	header.versionMajor     = 3;
	header.chunkCount       = 0;
	header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
	header.fileSize         = bytes.Size();
	bytes.PatchPod(0, header);
	const auto file = bytes.Take();

	REQUIRE_THROWS_WITH(
		Load(file),
		ContainsSubstring("test: format 3 predates the schema table and cannot be read"));
}

TEST_CASE("what no rule converts names the container, the layout and the field", "[chunk][schema]")
{
	struct ThingWide
	{
		uint64_t id;      // was u32
		uint32_t weight;  // was f32
		uint32_t pad;
		StampNew stamp;
	};
	schema::Schema wide;
	schema::LayoutBuilder<StampNew>(wide, "Stamp")
		.AddField("size", &StampNew::size)
		.AddField("hash", &StampNew::hash)
		.Finish();
	schema::LayoutBuilder<ThingWide>(wide, "Thing")
		.AddField("id", &ThingWide::id)
		.AddField("weight", &ThingWide::weight)
		.AddField("pad", &ThingWide::pad)
		.AddField("stamp", &ThingWide::stamp)
		.Finish();

	const auto file = Write(wide, std::vector<ThingWide>{ { 1, 2, 0, { 3, 4 } } }, 10);
	REQUIRE_THROWS_WITH(
		Load(file),
		ContainsSubstring("test: Thing.id: file stores u64, engine wants u32, no conversion"));
}

TEST_CASE("the ranged read converts through the same schema", "[chunk][schema]")
{
	const auto file = Write(SchemaA(), std::vector<ThingA>{ { 7, 2.5f, { 100, 5 } } }, 10);
	const auto path = std::filesystem::temp_directory_path() / "schema_container_test.bin";
	{
		std::ofstream out(path, std::ios::binary);
		out.write(
			reinterpret_cast<const char*>(file.data()),
			static_cast<std::streamsize>(file.size()));
	}

	const std::array<uint32_t, 1> ids = { uint32_t(ChunkId::kThings) };
	const auto chunks                 = chunk::readChunksFromFile(path, c_Magic, 11, ids, "test");
	std::filesystem::remove(path);

	const auto things = chunks.Read<Thing>(uint32_t(ChunkId::kThings), CurrentSchema());
	REQUIRE(things.size() == 1);
	CHECK(things[0].id == 7);
	CHECK(things[0].weight == 2.5f);
	CHECK(things[0].stamp.hash == 0);  // no hook on this path: the ranged read is for references
	CHECK(chunks.GetStoredSchema().Find("Stamp") != nullptr);
	CHECK_FALSE(chunks.Contains(uint32_t(ChunkId::kNames)));  // not asked for, not read
}

TEST_CASE("a POD nobody registered cannot be written", "[chunk][schema]")
{
	struct Stray
	{
		uint32_t x;
	};
	chunk::Writer writer(CurrentSchema());
	REQUIRE_THROWS_WITH(
		writer.Add(uint32_t(ChunkId::kThings), std::vector<Stray>{ { 1 } }),
		ContainsSubstring("has no layout in the schema"));
	REQUIRE_THROWS_WITH(
		writer.Add(chunk::c_SchemaChunk, std::vector<uint32_t>{ 1 }),
		ContainsSubstring("id 0 is the schema's"));
}
