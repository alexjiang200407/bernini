#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/pak_io.h>
#include <assetlib/pak_pack.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

#include <catch2/matchers/catch_matchers_string.hpp>

#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "MountAt.h"
#include "RefsSandbox.h"
#include "SkinnedGltf.h"
#include "VatFixture.h"
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
		WriteSource(root.path / "textures_src/skin.ktx2", { { 200, 180, 160, 255 } });
		BakeAndSave(root, "skin.bmaterial", "textures_src/skin.ktx2");
		SaveMesh(root, "hero.bmesh", { "Materials/skin.bmaterial" }, "Skeletons/hero.bskel");

		Skeleton skeleton;
		Bone     bone{};
		bone.parent      = c_InvalidIndex;
		bone.inverseBind = glm::mat4(1.0f);
		bone.nameOffset  = skeleton.stringPool.add("root");
		skeleton.bones   = { bone };
		fs::create_directories(root.path / "Skeletons");
		StoreAt(root.path).Save(skeleton, "Skeletons/hero.bskel");

		const Environment environment = WriteEnvironment(root);

		// The things an archive must not carry, each for its own reason.
		{
			std::ofstream(root.path / "Test.berniniproject") << "editor metadata";
			std::ofstream(root.path / "hero.glb") << "awaiting import";
			fs::create_directories(root.path / "ShaderCache");
			std::ofstream(root.path / "ShaderCache/pipelines.psolib") << "per-machine";
			std::ofstream(root.path / ".overlay.json") << "{}";

			fs::create_directories(root.path / "meshes_src");
			std::ofstream(root.path / "meshes_src/kirk.glb") << "the imported source";
			core::file::write_atomic(
				root.path / "meshes_src/kirk.bimport",
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
		const PackReport packed =
			packProject(AssetStore(root.path), PackDesc{ root.path / "Data.bpak" });
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
		CHECK(Contains(entries, "Meshes/hero.bmesh"));
		CHECK(Contains(entries, "Materials/skin.bmaterial"));
		CHECK(Contains(entries, "Skeletons/hero.bskel"));
		CHECK(Contains(entries, environment.env));
		CHECK(Contains(entries, environment.sky));
		CHECK(Contains(entries, environment.lighting));

		// The baked maps are most of an archive's bytes, and they are the half of the texture
		// story that ships -- the sources they were composited from are the half that does not.
		CHECK(Contains(entries, environment.skyBaked));
		CHECK(Contains(entries, environment.prefilter));
		CHECK(Contains(entries, environment.irradiance));
	}

	SECTION("authoring sources and non-assets are out")
	{
		CHECK_FALSE(Contains(entries, "textures_src/skin.ktx2"));
		CHECK_FALSE(Contains(entries, environment.skySource));
		CHECK_FALSE(Contains(entries, "Test.berniniproject"));
		CHECK_FALSE(Contains(entries, "hero.glb"));
		CHECK_FALSE(Contains(entries, "ShaderCache/pipelines.psolib"));
		CHECK_FALSE(Contains(entries, ".overlay.json"));

		// No entry anywhere under the authoring directory, however deep.
		for (const std::string& entry : entries)
			CHECK(entry.find("textures_src/") == std::string::npos);
	}

	SECTION("an imported source and its document are out, wherever they sit")
	{
		CHECK_FALSE(Contains(entries, "meshes_src/kirk.glb"));
		CHECK_FALSE(Contains(entries, "meshes_src/kirk.bimport"));
		// A stray document outside meshes_src is excluded by its *type*, not the directory --
		// the game never reads one, so it must not ride in on being a registered extension.
		CHECK_FALSE(Contains(entries, "stray.bimport"));
	}

	// An extension nothing claims is an extension the archive does not carry. Counting them is what
	// keeps that from being silent when a new runtime container is added and left unregistered.
	SECTION("what was skipped is reported rather than dropped quietly")
	{
		CHECK(report.skippedByExtension[".berniniproject"] == 1);
		CHECK(report.skippedByExtension[".glb"] == 1);
		CHECK(report.skippedByExtension[".psolib"] == 1);
		CHECK(report.skippedByExtension[".json"] == 1);

		// textures_src is excluded by directory, before the extension is consulted: `.ktx2` is a
		// registered type, and counting these as unclaimed would misreport why they are absent.
		CHECK(report.skippedByExtension.find(".ktx2") == report.skippedByExtension.end());
	}
}

TEST_CASE("every packed entry reads back byte-for-byte", "[pack]")
{
	const DataRoot root("pack_roundtrip");
	StageProject(root);

	const PackReport packed =
		packProject(AssetStore(root.path), PackDesc{ root.path / "Data.bpak" });

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

	static_cast<void>(packProject(AssetStore(root.path), PackDesc{ root.path / "a.bpak" }));
	static_cast<void>(packProject(AssetStore(root.path), PackDesc{ root.path / "b.bpak" }));

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
	static_cast<void>(packProject(AssetStore(root.path), PackDesc{ target }));

	const std::vector<std::byte>   before  = core::file::read_file_bytes(target.string());
	const std::vector<std::string> entries = PakFile(target).Enumerate();
	REQUIRE_FALSE(entries.empty());

	{
		PakWriter abandoned(target);
		abandoned.Add("Meshes/half.bmesh", std::vector<std::byte>(64, std::byte{ 0xAB }), {});
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

	WriteSource(root.path / "textures_src/skin.ktx2", { { 200, 180, 160, 255 } });

	// Routed, never baked: it draws from the authoring source that packing excludes.
	BMaterial unbaked;
	unbaked.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };
	StoreAt(root.path).Save(unbaked, "Materials/unbaked.bmaterial");

	// Baked: it draws the triplet, which packing carries.
	BakeAndSave(root, "baked.bmaterial", "textures_src/skin.ktx2");

	PackReport report;
	static_cast<void>(PackAndEnumerate(root, &report));

	CHECK(
		report.materialsDrawingLoose == std::vector<std::string>{ "Materials/unbaked.bmaterial" });
}

namespace
{
	// A rig on disk, and the `.bvat` baked from it, both under `root`.
	void
	StageRig(const DataRoot& root)
	{
		const VatFixture fixture;

		fs::create_directories(root.path / "Skeletons");
		fs::create_directories(root.path / "Animations");

		StoreAt(root.path).Save(fixture.mesh, "Meshes/rig.bmesh");
		StoreAt(root.path).Save(fixture.skeleton, "Skeletons/rig.bskel");
		StoreAt(root.path).Save(fixture.animations, "Animations/rig.banim");

		StoreAt(root.path).Save(
			AssetStore(root.path).BakeVat(
				VatBakeDesc{ "Meshes/rig.bmesh", "Animations/rig.banim" }),
			"Meshes/rig.bvat");
	}
}

/**
 * What makes a packed `.bvat` trustworthy: its inputs may ship beside it with nowhere to write a
 * re-bake, so packing is the last moment it can be made correct. Task 8's read-only mount skips the
 * staleness check entirely on the strength of this.
 */
TEST_CASE("pack re-bakes a stale .bvat, and leaves a current one alone", "[pack][vat]")
{
	const DataRoot root("pack_vat");
	StageRig(root);

	SECTION("a current bake is packed untouched")
	{
		PackReport                     report;
		const std::vector<std::string> entries = PackAndEnumerate(root, &report);

		CHECK(report.vatsRebaked == 0);
		CHECK(Contains(entries, "Meshes/rig.bvat"));
	}

	SECTION("a stale bake is re-baked first, and what is packed is not stale")
	{
		// Longer, not merely rewritten: the stamp is size + whole seconds, so a same-second edit of
		// equal length would not move it.
		VatFixture edited;
		edited.animations.stringPool.add("padding-so-the-size-moves");
		StoreAt(root.path).Save(edited.animations, "Animations/rig.banim");

		REQUIRE(vatIsStale(loadVatTables(root.path / "Meshes/rig.bvat"), MountAt(root.path)));

		PackReport report;
		static_cast<void>(PackAndEnumerate(root, &report));

		CHECK(report.vatsRebaked == 1);

		// Judged inside the archive, which is where a shipped build asks the question.
		const PakFile pak(root.path / "Data.bpak");
		CHECK_FALSE(vatIsStale(loadVatTables(pak, "Meshes/rig.bvat"), pak));
	}
}

// Loud rather than silently shipping: a `.bvat` from another bake revision names its inputs in a
// layout this build does not vouch for, so pack cannot re-bake it. The loose project heals one on
// its next load, and that has to happen before an export.
TEST_CASE("pack refuses a .bvat from another bake revision", "[pack][vat]")
{
	const DataRoot root("pack_vat_old");
	StageRig(root);

	test::TamperHeaderByte(root.path / "Meshes/rig.bvat", test::c_TokenOffset);

	CHECK_THROWS_WITH(
		packProject(AssetStore(root.path), PackDesc{ root.path / "Data.bpak" }),
		Catch::Matchers::ContainsSubstring("another bake revision"));
}

// Loud rather than a quietly incomplete archive: a `.bvat` naming an input that is gone cannot be
// made correct, and shipping the stale one is the outcome packing exists to prevent.
TEST_CASE("pack fails when a stale .bvat cannot be re-baked", "[pack][vat]")
{
	const DataRoot root("pack_vat_broken");
	StageRig(root);

	VatFixture edited;
	edited.animations.stringPool.add("padding-so-the-size-moves");
	StoreAt(root.path).Save(edited.animations, "Animations/rig.banim");
	fs::remove(root.path / "Skeletons/rig.bskel");

	CHECK_THROWS_AS(
		packProject(AssetStore(root.path), PackDesc{ root.path / "Data.bpak" }),
		std::runtime_error);
}

TEST_CASE(
	"a stale group re-bakes into the archive, a rebind rides along, disk untouched",
	"[pack][regen]")
{
	const test::DataRoot    root("bernini_pack_regen");
	const test::SkinnedGltf source("bernini_pack_regen_gltf");
	test::ImportUnitGroup(root.path, source.PackGlb());

	const auto meshPath = root.path / "Meshes/unit.bmesh";
	test::TamperHeaderByte(meshPath, test::c_TokenOffset);
	rebindSubmeshInDocument(root.path, "meshes_src/unit.glb", "body", "Materials/blue.bmaterial");
	const auto stale = core::file::read_file_bytes(meshPath.string());

	const std::filesystem::path target = root.path / "Data.bpak";
	const PackReport            report = packProject(AssetStore(root.path), PackDesc{ target });
	CHECK(report.geometryRebaked >= 1);

	// The archive carries the current cook with the document's binding baked in -- a read-only
	// store trusts it, which is exactly what pack just made true.
	const AssetStore packed(root.path, std::make_shared<PakFile>(target));
	const BMesh      mesh = packed.Load<BMesh>("Meshes/unit.bmesh");
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Materials/blue.bmaterial");

	// In the archive only: the stale file on disk is migrate's to rewrite, never pack's.
	CHECK(core::file::read_file_bytes(meshPath.string()) == stale);
}

TEST_CASE("a group the seam cannot serve fails the pack", "[pack][regen]")
{
	const test::DataRoot    root("bernini_pack_unbakeable");
	const test::SkinnedGltf source("bernini_pack_unbakeable_gltf");
	test::ImportUnitGroup(root.path, source.PackGlb());

	test::TamperHeaderByte(root.path / "Meshes/unit.bmesh", test::c_TokenOffset);
	std::filesystem::remove(root.path / "meshes_src/unit.glb");

	CHECK_THROWS(packProject(AssetStore(root.path), PackDesc{ root.path / "Data.bpak" }));
}

TEST_CASE("a packed .bvat answers fresh inside the archive it shipped in", "[pack][regen][vat]")
{
	const test::DataRoot    root("bernini_pack_vat_regen");
	const test::SkinnedGltf source("bernini_pack_vat_regen_gltf");
	test::ImportUnitGroup(root.path, source.PackGlb());

	const AssetStore  store(root.path);
	const std::string vatKey =
		vatPathFor("Meshes/unit.bmesh", "Animations/unit.banim").generic_string();
	SaveAt(
		store.BakeVat(VatBakeDesc{ "Meshes/unit.bmesh", "Animations/unit.banim" }),
		root.path / vatKey);

	// The group goes stale with every byte and stamp the bake recorded still holding: a
	// parameter edit moves only the document, so nothing but the group axis can see it -- the
	// shape a re-export or a sibling's merge leaves when nobody ran migrate.
	auto document       = loadImportDocument(root.path / "meshes_src/unit.bimport");
	document.sampleRate = 60.0f;
	core::file::write_atomic(
		root.path / "meshes_src/unit.bimport",
		AssetCodec<ImportDocument>::Serialize(document));

	const std::filesystem::path target = root.path / "Data.bpak";
	const PackReport            report = packProject(store, PackDesc{ target });
	CHECK(report.vatsRebaked == 1);  // the group axis alone fired

	// Judged inside the archive, which is where a shipped build asks: the vat's stamps must
	// describe the geometry as archived -- the seam's answers -- not the stale file the bake
	// read them beside.
	const AssetStore packed(root.path, std::make_shared<PakFile>(target));
	CHECK_FALSE(packed.VatIsStale(packed.LoadVatTables(vatKey)));
}
