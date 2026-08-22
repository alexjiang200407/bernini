#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/container_info.h>
#include <assetlib/migrate.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/magic.h>

#include <core/file/file.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

namespace
{
	struct Project
	{
		std::filesystem::path root;

		Project()
		{
			root = std::filesystem::temp_directory_path() / "assetlib_migrate_test";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Materials");
		}

		~Project() { std::filesystem::remove_all(root); }

		void
		Write(const std::filesystem::path& relative, std::span<const std::byte> bytes) const
		{
			std::ofstream out(root / relative, std::ios::binary);
			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}

		std::vector<std::byte>
		Read(const std::filesystem::path& relative) const
		{
			return core::file::read_file_bytes((root / relative).string());
		}
	};

	std::vector<std::byte>
	MaterialBytes(std::string_view name)
	{
		BMaterial material;
		material.name = std::string(name);
		return serializeMaterial(material);
	}

	/** The same content spelled non-canonically: what a hand edit or a merge leaves behind. */
	std::vector<std::byte>
	Older()
	{
		constexpr std::string_view c_Older = R"({"shadingModel": "pbr", "name": "older"})";
		const auto                 data = std::as_bytes(std::span(c_Older.data(), c_Older.size()));
		return { data.begin(), data.end() };
	}
}

TEST_CASE(
	"migrate rewrites what is not current, leaves what is, and reports what it cannot read",
	"[migrate]")
{
	const Project project;
	project.Write("Materials/current.bmaterial", MaterialBytes("current"));
	project.Write("Materials/older.bmaterial", Older());
	const std::vector<std::byte> flat = { std::byte{ 'B' },
		                                  std::byte{ 'M' },
		                                  std::byte{ 'A' },
		                                  std::byte{ 'T' } };
	project.Write("Materials/flat.bmaterial", flat);  // a stream from before the schema chunk
	project.Write("notes.txt", flat);                 // not a container at all

	SECTION("a dry run reports and writes nothing")
	{
		const auto before = project.Read("Materials/older.bmaterial");
		const auto report = migrateProject(project.root, true);
		CHECK(report.Count(MigratedFile::Outcome::kUnchanged) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Materials/older.bmaterial") == before);
		CHECK(report.files.size() == 3);  // notes.txt was never a candidate
	}

	SECTION("a real run rewrites once, and the second run finds nothing to do")
	{
		const auto first = migrateProject(project.root, false);
		CHECK(first.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(first.Count(MigratedFile::Outcome::kFailed) == 1);

		const auto rewritten = project.Read("Materials/older.bmaterial");
		CHECK(rewritten == MaterialBytes("older"));  // stamped current again
		CHECK(deserializeMaterial(rewritten).name == "older");

		const auto second = migrateProject(project.root, false);
		CHECK(second.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(second.Count(MigratedFile::Outcome::kUnchanged) == 2);
		CHECK(second.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Materials/flat.bmaterial") == flat);  // never half-written
	}

	SECTION("the failure says which file, and why")
	{
		const auto report = migrateProject(project.root, true);
		const auto failed =
			std::ranges::find(report.files, MigratedFile::Outcome::kFailed, &MigratedFile::outcome);
		REQUIRE(failed != report.files.end());
		CHECK(failed->path.filename() == "flat.bmaterial");
		CHECK_THAT(failed->message, ContainsSubstring("bmaterial:"));
	}
}

TEST_CASE("migrate refuses a root that is not a directory", "[migrate]")
{
	REQUIRE_THROWS_WITH(
		migrateProject(std::filesystem::temp_directory_path() / "no_such_project_dir", true),
		ContainsSubstring("is not a directory"));
}

TEST_CASE(
	"a container says what it is and what it stores, without being loaded",
	"[migrate][describe]")
{
	// A schema rider: the env family still carries chunk 0 until its own conversion task; the
	// material this case used to inspect is an authored text document now, with no schema to show.
	const auto bytes = serializeSky(BSky());
	const auto info  = inspectContainer(bytes);
	CHECK(info.magic == magic::c_BSky);
	CHECK(info.schema.Find("SkyRecord") != nullptr);

	const std::string text = describe(info.schema);
	CHECK_THAT(text, ContainsSubstring("SkyRecord"));
	CHECK_THAT(text, ContainsSubstring("EnvMapRoute"));

	REQUIRE_THROWS_WITH(
		inspectContainer(std::span(bytes).first(8)),
		ContainsSubstring("stream shorter than a header"));
}
