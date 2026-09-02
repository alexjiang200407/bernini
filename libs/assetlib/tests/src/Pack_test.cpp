#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

#include <catch2/matchers/catch_matchers_string.hpp>

#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "MountAt.h"
#include "RefsSandbox.h"
#include "SkinnedGltf.h"
#include "mounted_io.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	// One of each kind the runtime reads, beside the authoring sources and the non-assets that
	// produce or accompany them.
	Environment
	StageProject(const DataRoot& root)
	{
		WriteSource(root.path / "Derived/SourceTextures/skin.ktx2", { { 200, 180, 160, 255 } });
		BakeAndSave(root, "skin.bmaterial", "Derived/SourceTextures/skin.ktx2");
		SaveMesh(
			root,
			"hero.bmesh",
			{ "Authored/Materials/skin.bmaterial" },
			"Derived/Skeletons/hero.bskel");

		Skeleton skeleton;
		Bone     bone{};
		bone.parent      = c_InvalidIndex;
		bone.inverseBind = glm::mat4(1.0f);
		bone.nameOffset  = skeleton.stringPool.add("root");
		skeleton.bones   = { bone };
		fs::create_directories(root.path / "Derived/Skeletons");
		StoreAt(root.path).Save(skeleton, "Derived/Skeletons/hero.bskel");

		const Environment environment = WriteEnvironment(root);

		// The UI runtime's three: text and a font binary the game reads, none of them a container
		// this library encodes.
		fs::create_directories(root.path / "Authored/UI");
		fs::create_directories(root.path / "Authored/Fonts");
		std::ofstream(root.path / "Authored/UI/menu.rml") << "<rml><body>menu</body></rml>";
		std::ofstream(root.path / "Authored/UI/menu.rcss") << "body { color: #fff; }";
		std::ofstream(root.path / "Authored/Fonts/ui.ttf") << "not really a font";

		// The things an archive must not carry, each for its own reason.
		{
			std::ofstream(root.path / "Test.bproj") << "editor metadata";
			std::ofstream(root.path / "hero.glb") << "awaiting import";
			fs::create_directories(root.path / "ShaderCache");
			std::ofstream(root.path / "ShaderCache/pipelines.psolib") << "per-machine";
			std::ofstream(root.path / ".overlay.json") << "{}";

			fs::create_directories(root.path / "Authored/Meshes");
			std::ofstream(root.path / "Authored/Meshes/kirk.glb") << "the imported source";
			core::file::write_atomic(
				root.path / "Authored/Meshes/kirk.bimport",
				AssetCodec<ImportDocument>::Serialize(ImportDocument{}));
			core::file::write_atomic(
				root.path / "stray.bimport",
				AssetCodec<ImportDocument>::Serialize(ImportDocument{}));
		}

		return environment;
	}

	std::vector<std::string>
	PackAndEnumerate(const DataRoot& root, PackReport* report = nullptr)
	{
		const PackReport packed = AssetStore(root.path).Pack(PackDesc{ root.path / "Data.bpak" });
		if (report != nullptr)
			*report = packed;

		return PakFile(root.path / "Data.bpak").Enumerate();
	}

	bool
	Contains(const std::vector<std::string>& entries, std::string_view path)
	{
		return std::ranges::find(entries, path) != entries.end();
	}
}

TEST_CASE("pack carries what the runtime reads and nothing that produces it", "[pack]")
{
	const DataRoot    root("pack_exclusion");
	const Environment environment = StageProject(root);

	PackReport                     report;
	const std::vector<std::string> entries = PackAndEnumerate(root, &report);

	SECTION("every asset type is in")
	{
		CHECK(Contains(entries, "Derived/Meshes/hero.bmesh"));
		CHECK(Contains(entries, "Authored/Materials/skin.bmaterial"));
		CHECK(Contains(entries, "Derived/Skeletons/hero.bskel"));
		CHECK(Contains(entries, environment.env));
		CHECK(Contains(entries, environment.sky));
		CHECK(Contains(entries, environment.lighting));

		// The baked maps are most of an archive's bytes, and they are the half of the texture
		// story that ships -- the sources they were composited from are the half that does not.
		CHECK(Contains(entries, environment.skyBaked));
		CHECK(Contains(entries, environment.prefilter));
		CHECK(Contains(entries, environment.irradiance));

		// A UI document, its stylesheet and its font are read at runtime like any other asset.
		CHECK(Contains(entries, "Authored/UI/menu.rml"));
		CHECK(Contains(entries, "Authored/UI/menu.rcss"));
		CHECK(Contains(entries, "Authored/Fonts/ui.ttf"));
	}

	SECTION("authoring sources and non-assets are out")
	{
		CHECK_FALSE(Contains(entries, "Derived/SourceTextures/skin.ktx2"));
		CHECK_FALSE(Contains(entries, environment.skySource));
		CHECK_FALSE(Contains(entries, "Test.bproj"));
		CHECK_FALSE(Contains(entries, "hero.glb"));
		CHECK_FALSE(Contains(entries, "ShaderCache/pipelines.psolib"));
		CHECK_FALSE(Contains(entries, ".overlay.json"));

		// No entry anywhere under the authoring directory, however deep.
		for (const std::string& entry : entries)
			CHECK(entry.find("Derived/SourceTextures/") == std::string::npos);
	}

	SECTION("an imported source and its document are out, wherever they sit")
	{
		CHECK_FALSE(Contains(entries, "Authored/Meshes/kirk.glb"));
		CHECK_FALSE(Contains(entries, "Authored/Meshes/kirk.bimport"));
		// A stray document outside Authored/Meshes is excluded by its *type*, not the directory --
		// the game never reads one, so it must not ride in on being a registered extension.
		CHECK_FALSE(Contains(entries, "stray.bimport"));
	}

	// An extension nothing claims is an extension the archive does not carry. Counting them is what
	// keeps that from being silent when a new runtime container is added and left unregistered.
	SECTION("what was skipped is reported rather than dropped quietly")
	{
		CHECK(report.skippedByExtension[".bproj"] == 1);
		CHECK(report.skippedByExtension[".glb"] == 1);
		CHECK(report.skippedByExtension[".psolib"] == 1);
		CHECK(report.skippedByExtension[".json"] == 1);

		// Derived/SourceTextures is excluded by directory, before the extension is consulted:
		// `.ktx2` is a
		// registered type, and counting these as unclaimed would misreport why they are absent.
		CHECK(report.skippedByExtension.find(".ktx2") == report.skippedByExtension.end());

		// The UI's three are claimed kinds, so they pack rather than being counted as unknown --
		// which is what an unregistered extension would look like here.
		CHECK(report.skippedByExtension.find(".rml") == report.skippedByExtension.end());
		CHECK(report.skippedByExtension.find(".rcss") == report.skippedByExtension.end());
		CHECK(report.skippedByExtension.find(".ttf") == report.skippedByExtension.end());
	}
}

TEST_CASE("every packed entry reads back byte-for-byte", "[pack]")
{
	const DataRoot root("pack_roundtrip");
	StageProject(root);

	const PackReport packed = AssetStore(root.path).Pack(PackDesc{ root.path / "Data.bpak" });

	const core::file::LooseFileSystem loose(root.path);
	const PakFile                     pak(root.path / "Data.bpak");

	REQUIRE(packed.entries != 0);
	REQUIRE(pak.Enumerate().size() == packed.entries);

	for (const std::string& entry : pak.Enumerate())
	{
		CHECK(pak.Read(entry) == loose.Read(entry));
		CHECK(pak.Stat(entry) == loose.Stat(entry));
	}
}

/**
 * Packing one tree twice gives one archive. A shipped artifact that differed run to run would defeat
 * every downstream check that compares builds.
 *
 * It is the *walk* that is sorted, not just the entry table. Sorting the table alone would not be
 * enough: `PakWriter` streams each payload as it arrives, so Add order is payload order, and two
 * walks that met the same files in a different order would record different offsets for every path.
 * Two runs on one machine agree either way, which is the limit of what this can reach from here --
 * the case it is really written against is two filesystems disagreeing about iteration order.
 */
TEST_CASE("packing the same tree twice produces identical bytes", "[pack]")
{
	const DataRoot root("pack_determinism");
	StageProject(root);

	static_cast<void>(AssetStore(root.path).Pack(PackDesc{ root.path / "a.bpak" }));
	static_cast<void>(AssetStore(root.path).Pack(PackDesc{ root.path / "b.bpak" }));

	CHECK(
		core::file::read_file_bytes((root.path / "a.bpak").string()) ==
		core::file::read_file_bytes((root.path / "b.bpak").string()));
}

/**
 * The archive at the target is a shipped artifact, not a cache entry that could simply miss, so a
 * pack that dies partway must leave the previous one whole.
 *
 * `PakWriter` streams into a temp beside the target and renames only once the table is written, so
 * the target is untouched until the moment it is replaced. This exercises that at the level it is
 * guaranteed: a writer abandoned over an existing archive.
 */
TEST_CASE("an interrupted pack leaves the previous archive intact", "[pack]")
{
	const DataRoot root("pack_interrupted");
	StageProject(root);

	const auto target = root.path / "Data.bpak";
	static_cast<void>(AssetStore(root.path).Pack(PackDesc{ target }));

	const std::vector<std::byte>   before  = core::file::read_file_bytes(target.string());
	const std::vector<std::string> entries = PakFile(target).Enumerate();
	REQUIRE_FALSE(entries.empty());

	{
		PakWriter abandoned(target);
		abandoned.Add(
			"Derived/Meshes/half.bmesh",
			std::vector<std::byte>(64, std::byte{ 0xAB }),
			{});
		// No Finish: the destructor removes the temp.
	}

	CHECK(core::file::read_file_bytes(target.string()) == before);
	CHECK(PakFile(target).Enumerate() == entries);
}

// The exclusion rule is right and the material is simply not baked, but the archive is valid either
// way -- so nothing except this report would say that what shipped has nothing to sample.
TEST_CASE("a material that draws loose is named, because the archive drops its sources", "[pack]")
{
	const DataRoot root("pack_loose_material");

	WriteSource(root.path / "Derived/SourceTextures/skin.ktx2", { { 200, 180, 160, 255 } });

	// Routed, never baked: it draws from the authoring source that packing excludes.
	BMaterial unbaked;
	unbaked.pbr.routes[0] = { "Derived/SourceTextures/skin.ktx2", 0 };
	StoreAt(root.path).Save(unbaked, "Authored/Materials/unbaked.bmaterial");

	// Baked: it draws the triplet, which packing carries.
	BakeAndSave(root, "baked.bmaterial", "Derived/SourceTextures/skin.ktx2");

	PackReport report;
	static_cast<void>(PackAndEnumerate(root, &report));

	CHECK(
		report.materialsDrawingLoose ==
		std::vector<std::string>{ "Authored/Materials/unbaked.bmaterial" });
}

TEST_CASE(
	"a stale group re-bakes into the archive, a rebind rides along, disk untouched",
	"[pack][regen]")
{
	const test::DataRoot    root("bernini_pack_regen");
	const test::SkinnedGltf source("bernini_pack_regen_gltf");
	test::ImportUnitGroup(root.path, source.PackGlb());

	const auto meshPath = root.path / "Derived/Meshes/unit.bmesh";
	test::TamperHeaderByte(meshPath, test::c_TokenOffset);
	AssetStore(root.path).RebindSubmeshInDocument(
		"Authored/Meshes/unit.glb",
		"body",
		"Authored/Materials/blue.bmaterial");
	const auto stale = core::file::read_file_bytes(meshPath.string());

	const std::filesystem::path target = root.path / "Data.bpak";
	const PackReport            report = AssetStore(root.path).Pack(PackDesc{ target });
	CHECK(report.geometryRebaked >= 1);

	// The archive carries the current cook with the document's binding baked in -- a read-only
	// store trusts it, which is exactly what pack just made true.
	const AssetStore packed(root.path, std::make_shared<PakFile>(target));
	const BMesh      mesh = packed.Load<BMesh>("Derived/Meshes/unit.bmesh");
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Authored/Materials/blue.bmaterial");

	// In the archive only: the stale file on disk is migrate's to rewrite, never pack's.
	CHECK(core::file::read_file_bytes(meshPath.string()) == stale);
}

TEST_CASE("a group the seam cannot serve fails the pack", "[pack][regen]")
{
	const test::DataRoot    root("bernini_pack_unbakeable");
	const test::SkinnedGltf source("bernini_pack_unbakeable_gltf");
	test::ImportUnitGroup(root.path, source.PackGlb());

	test::TamperHeaderByte(root.path / "Derived/Meshes/unit.bmesh", test::c_TokenOffset);
	std::filesystem::remove(root.path / "Authored/Meshes/unit.glb");

	CHECK_THROWS(AssetStore(root.path).Pack(PackDesc{ root.path / "Data.bpak" }));
}
