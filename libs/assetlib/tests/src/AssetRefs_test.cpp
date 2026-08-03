#include <assetlib/asset_refs.h>

#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>

#include "bmesh_texture.h"

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	// A scratch data root that cleans up after itself.
	struct DataRoot
	{
		fs::path path;

		explicit DataRoot(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Materials");
			fs::create_directories(path / "Meshes");
			fs::create_directories(path / "textures_src");
		}
		~DataRoot() { fs::remove_all(path); }

		AssetRefScanDesc
		Desc() const
		{
			return AssetRefScanDesc{ path };
		}

		AssetRefGraph
		Scan() const
		{
			return AssetRefGraph::Scan(Desc());
		}
	};

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	void
	WriteSource(const fs::path& path, std::array<uint8_t, 4> rgba)
	{
		constexpr uint32_t c_Size = 16;

		std::vector<std::byte> pixels(static_cast<size_t>(c_Size) * c_Size * 4);
		for (size_t t = 0; t < static_cast<size_t>(c_Size) * c_Size; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		fs::create_directories(path.parent_path());
		writeKTX2(rgba8ToImage(pixels, c_Size, c_Size), path, false, Ktx2Compression::kNone);
	}

	/**
	 * Bakes a material whose base colour reads `source` (relative to the root), saves it as
	 * `Materials/<name>`, and returns it -- so a test can name the maps the bake wrote.
	 */
	BMaterial
	BakeAndSave(const DataRoot& root, const char* name, const char* source)
	{
		BMaterial material;
		material.pbr.routes[0] = { source, 0 };

		bakeMaterial(material, MaterialBakeDesc{ root.path });
		saveMaterial(material, root.path / "Materials" / name);
		return material;
	}

	/** A minimal but loadable mesh whose submeshes name `materials`, one slot each. */
	BMesh
	MakeMesh(const std::vector<std::string>& materials)
	{
		BMesh mesh;
		mesh.stringPool.push_back('\0');

		Node root{};
		root.localTransform = { glm::vec3(0.0f),
			                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                    glm::vec3(1.0f) };
		root.parent         = c_InvalidIndex;
		root.firstChild     = c_InvalidIndex;
		root.nextSibling    = c_InvalidIndex;
		root.mesh           = 0;
		root.nameOffset     = 0;
		mesh.nodes          = { root };
		mesh.roots          = { 0 };

		for (uint32_t i = 0; i < materials.size(); ++i)
		{
			Submesh submesh{};
			submesh.indexType = IndexType::kUint16;
			submesh.material  = i;
			mesh.submeshes.push_back(submesh);
		}

		mesh.meshes    = { Mesh{ 0, static_cast<uint32_t>(materials.size()), 0 } };
		mesh.materials = materials;
		return mesh;
	}

	void
	SaveMesh(const DataRoot& root, const char* name, const std::vector<std::string>& materials)
	{
		save(MakeMesh(materials), root.path / "Meshes" / name);
	}

	/** The referrers of `asset`, as plain paths, so a test can compare against what it wrote. */
	std::vector<std::string>
	ReferrerPaths(const AssetRefGraph& graph, std::string_view asset)
	{
		std::vector<std::string> out;
		for (const AssetRef& ref : graph.ReferrersOf(asset)) out.push_back(ref.referrer);
		return out;
	}
}

TEST_CASE("loadMaterialPaths reads what a full load would, without the geometry", "[assetrefs]")
{
	// The scan surveys every mesh in the project, and a .bmesh is mostly vertex data -- so it seeks to
	// the material chunk instead of deserializing. This pins the cheap reader against the real one; a
	// change to the container that broke it would otherwise surface as an asset silently losing its
	// references.
	const DataRoot root("bernini_refs_matpaths");

	const std::vector<std::string> materials = { "Materials/a.bmaterial",
		                                         "Materials/nested/b.bmaterial",
		                                         "Materials/a.bmaterial" };
	SaveMesh(root, "mesh.bmesh", materials);

	const fs::path file = root.path / "Meshes" / "mesh.bmesh";

	CHECK(loadMaterialPaths(file) == load(file).materials);
	CHECK(loadMaterialPaths(file) == materials);

	SECTION("a mesh that names no material yields none, and is not an error")
	{
		SaveMesh(root, "bare.bmesh", {});
		CHECK(loadMaterialPaths(root.path / "Meshes" / "bare.bmesh").empty());
	}

	SECTION("a file that is not a mesh is an error, not an empty list")
	{
		std::ofstream(root.path / "Meshes" / "junk.bmesh", std::ios::binary) << "not a mesh";
		CHECK_THROWS_AS(loadMaterialPaths(root.path / "Meshes" / "junk.bmesh"), std::runtime_error);
	}
}

TEST_CASE("A material references both the maps it baked and the sources it routes", "[assetrefs]")
{
	// The two kinds are not interchangeable: the triplet is what the renderer samples, the routes are
	// what a re-bake reads. Deleting either breaks the material, so both are edges.
	const DataRoot root("bernini_refs_material");

	WriteSource(root.path / "textures_src" / "albedo.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "textures_src/albedo.ktx2");

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.materialsScanned == 1);

	SECTION("the baked map is referenced by the material that wrote it")
	{
		const auto referrers = graph.ReferrersOf(material.pbr.baseColorTexture);

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Materials/mat.bmaterial");
		CHECK(referrers[0].kind == RefKind::kBakedMap);
	}

	SECTION("so is the source the channel routes from")
	{
		const auto referrers = graph.ReferrersOf("textures_src/albedo.ktx2");

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Materials/mat.bmaterial");
		CHECK(referrers[0].kind == RefKind::kChannelRoute);
	}

	SECTION("neither can be deleted while the material names it")
	{
		for (const std::string& texture :
		     { material.pbr.baseColorTexture, std::string("textures_src/albedo.ktx2") })
		{
			INFO("texture: " << texture);

			const DeletionPlan plan = planDeletion(graph, texture);

			CHECK_FALSE(plan.Allowed());
			CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kRefused);
			CHECK(fs::exists(root.path / texture));
		}
	}
}

TEST_CASE("A texture no material names can be deleted", "[assetrefs]")
{
	const DataRoot root("bernini_refs_unused");

	WriteSource(root.path / "textures_src" / "orphan.ktx2", { { 0, 200, 0, 255 } });

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.ReferrersOf("textures_src/orphan.ktx2").empty());

	const DeletionPlan plan = planDeletion(graph, "textures_src/orphan.ktx2");
	REQUIRE(plan.Allowed());
	REQUIRE(plan.assetType == AssetType::kTexture);

	CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
	CHECK_FALSE(fs::exists(root.path / "textures_src" / "orphan.ktx2"));
}

TEST_CASE("A material a mesh names cannot be deleted", "[assetrefs]")
{
	const DataRoot root("bernini_refs_meshmaterial");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "used.bmaterial", "textures_src/a.ktx2");
	BakeAndSave(root, "loose.bmaterial", "textures_src/a.ktx2");

	SaveMesh(root, "mesh.bmesh", { "Materials/used.bmaterial" });

	const AssetRefGraph graph = root.Scan();

	REQUIRE(graph.meshesScanned == 1);

	SECTION("the one it names is held")
	{
		const auto referrers = graph.ReferrersOf("Materials/used.bmaterial");

		REQUIRE(referrers.size() == 1);
		CHECK(referrers[0].referrer == "Meshes/mesh.bmesh");
		CHECK(referrers[0].kind == RefKind::kSubmeshMaterial);

		CHECK_FALSE(planDeletion(graph, "Materials/used.bmaterial").Allowed());
	}

	SECTION("the one no mesh names is not")
	{
		const DeletionPlan plan = planDeletion(graph, "Materials/loose.bmaterial");

		REQUIRE(plan.Allowed());
		REQUIRE(plan.assetType == AssetType::kMaterial);
		CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
	}
}

TEST_CASE("A mesh is always deletable, and its materials outlive it", "[assetrefs]")
{
	// The rule the feature exists for. A material is a shareable asset that a mesh happens to name --
	// not a part of it -- so deleting the mesh must not take it down, and nothing references a .bmesh
	// for it to be blocked by.
	const DataRoot root("bernini_refs_meshdelete");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	SaveMesh(root, "mesh.bmesh", { "Materials/mat.bmaterial" });

	const AssetRefGraph graph = root.Scan();
	const DeletionPlan  plan  = planDeletion(graph, "Meshes/mesh.bmesh");

	REQUIRE(plan.Allowed());
	REQUIRE(plan.assetType == AssetType::kMesh);
	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

	CHECK_FALSE(fs::exists(root.path / "Meshes" / "mesh.bmesh"));

	// Nothing else was touched: not the material, and not the maps it baked.
	CHECK(fs::exists(root.path / "Materials" / "mat.bmaterial"));
	CHECK(fs::exists(root.path / material.pbr.baseColorTexture));
	CHECK(fs::exists(root.path / "textures_src" / "a.ktx2"));

	SECTION("and the material it freed can then be deleted in its own right")
	{
		const AssetRefGraph after = root.Scan();

		CHECK(after.ReferrersOf("Materials/mat.bmaterial").empty());
		CHECK(planDeletion(after, "Materials/mat.bmaterial").Allowed());
	}
}

TEST_CASE("A mesh naming one material twice is one blocker, not two", "[assetrefs]")
{
	// attachMaterial splits a shared slot rather than repointing its siblings, so a .bmesh legitimately
	// names one material from two slots -- Test Project's tree_alpha_test.bmesh does. Reporting the same
	// mesh twice would be a lie about how much is holding the material.
	const DataRoot root("bernini_refs_dedup");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "leaf.bmaterial", "textures_src/a.ktx2");

	SaveMesh(
		root,
		"tree.bmesh",
		{ "Materials/leaf.bmaterial", "Materials/wood.bmaterial", "Materials/leaf.bmaterial" });

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, "Materials/leaf.bmaterial") ==
		std::vector<std::string>{ "Meshes/tree.bmesh" });
	CHECK(planDeletion(graph, "Materials/leaf.bmaterial").blockers.size() == 1);
}

TEST_CASE("A material routing one texture into two channels is one blocker", "[assetrefs]")
{
	// The same collapse on the other edge: an ORM map feeds roughness and metallic from one file.
	const DataRoot root("bernini_refs_dedup_routes");

	WriteSource(root.path / "textures_src" / "orm.ktx2", { { 10, 60, 90, 255 } });

	BMaterial material;
	material.pbr.routes[channelIndex(PbrChannel::kRoughness)] = { "textures_src/orm.ktx2", 1 };
	material.pbr.routes[channelIndex(PbrChannel::kMetallic)]  = { "textures_src/orm.ktx2", 2 };
	saveMaterial(material, root.path / "Materials" / "mat.bmaterial");

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, "textures_src/orm.ktx2") ==
		std::vector<std::string>{ "Materials/mat.bmaterial" });
}

TEST_CASE("A baked map two materials share is blocked by both", "[assetrefs]")
{
	// Baked maps are content-hashed, so two materials routing a group identically converge on one file.
	// A user told only one of them is holding it would delete the other's map and not know.
	const DataRoot root("bernini_refs_shared");

	WriteSource(root.path / "textures_src" / "shared.ktx2", { { 10, 60, 90, 255 } });

	const BMaterial first  = BakeAndSave(root, "first.bmaterial", "textures_src/shared.ktx2");
	const BMaterial second = BakeAndSave(root, "second.bmaterial", "textures_src/shared.ktx2");

	REQUIRE(first.pbr.baseColorTexture == second.pbr.baseColorTexture);

	const AssetRefGraph graph = root.Scan();

	CHECK(
		ReferrerPaths(graph, first.pbr.baseColorTexture) ==
		std::vector<std::string>{ "Materials/first.bmaterial", "Materials/second.bmaterial" });
	CHECK(planDeletion(graph, first.pbr.baseColorTexture).blockers.size() == 2);
}

TEST_CASE("Deleting a material leaves its maps for the prune to sweep", "[assetrefs]")
{
	// Deletion is not cascading, and does not need to be: the maps a deleted material alone named are
	// exactly what findUnusedBakedTextures already collects. The two features compose rather than
	// duplicate.
	const DataRoot root("bernini_refs_compose");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	const DeletionPlan plan = planDeletion(root.Scan(), "Materials/mat.bmaterial");
	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

	CHECK(fs::exists(root.path / material.pbr.baseColorTexture));

	const auto swept = findUnusedBakedTextures(TexturePruneDesc{ root.path });

	REQUIRE(swept.unused.size() == 1);
	CHECK(swept.unused.front().path == material.pbr.baseColorTexture);
}

TEST_CASE("A referrer that cannot be read stops the scan", "[assetrefs]")
{
	// The fail-safe. A material or mesh we cannot parse is one whose edges we cannot see, and we would
	// then let the user delete straight through them. Refusing to answer is the only safe answer.
	const DataRoot root("bernini_refs_corrupt");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "good.bmaterial", "textures_src/a.ktx2");

	SECTION("an unreadable material")
	{
		std::ofstream(root.path / "Materials" / "broken.bmaterial", std::ios::binary)
			<< "not a material";

		CHECK_THROWS_AS(root.Scan(), std::runtime_error);
	}

	SECTION("an unreadable mesh")
	{
		std::ofstream(root.path / "Meshes" / "broken.bmesh", std::ios::binary) << "not a mesh";

		CHECK_THROWS_AS(root.Scan(), std::runtime_error);
	}
}

TEST_CASE("planDeletion refuses a file that is not an asset", "[assetrefs]")
{
	const DataRoot      root("bernini_refs_kind");
	const AssetRefGraph graph = root.Scan();

	CHECK_THROWS_AS(planDeletion(graph, "Levels/level.txt"), std::runtime_error);

	SECTION("and assetTypeFromExtension knows the three that are, whatever their case")
	{
		CHECK(assetTypeFromExtension("Meshes/a.bmesh") == AssetType::kMesh);
		CHECK(assetTypeFromExtension("Materials/a.bmaterial") == AssetType::kMaterial);
		CHECK(assetTypeFromExtension("Textures/a.ktx2") == AssetType::kTexture);
		CHECK(assetTypeFromExtension("Textures/A.KTX2") == AssetType::kTexture);

		CHECK(assetTypeFromExtension("game.berniniproject") == std::nullopt);
		CHECK(assetTypeFromExtension("notes.txt") == std::nullopt);
		CHECK(assetTypeFromExtension("Meshes") == std::nullopt);  // a directory is not an asset
	}
}

TEST_CASE("One asset is one key, however its path is spelled", "[assetrefs]")
{
	const DataRoot root("bernini_refs_normalize");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	const AssetRefGraph graph = root.Scan();

	// The path a file browser hands over need not be spelled the way the bake wrote it.
	CHECK(graph.IsReferenced("textures_src/a.ktx2"));
	CHECK(graph.IsReferenced("./textures_src/a.ktx2"));
	CHECK(graph.IsReferenced("Meshes/../textures_src/a.ktx2"));
}

TEST_CASE("An asset deleted behind the editor's back is not fatal", "[assetrefs]")
{
	// The data root is shared with the user's file manager. A texture removed there leaves the material
	// naming a file that is gone -- and if that stopped the scan, one stray deletion would make every
	// deletion in the project impossible.
	const DataRoot root("bernini_refs_external_texture");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "textures_src" / "b.ktx2", { { 0, 200, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");

	fs::remove(root.path / "textures_src" / "a.ktx2");

	const AssetRefGraph graph = root.Scan();

	SECTION("the dangling reference is reported")
	{
		REQUIRE(graph.broken.size() == 1);
		CHECK(graph.broken.front().target == "textures_src/a.ktx2");
		CHECK(graph.broken.front().referrer == "Materials/mat.bmaterial");
	}

	SECTION("and the material's other edges are still known")
	{
		// The referrer parsed; only its target was missing. Its baked map is still held.
		CHECK(graph.materialsScanned == 1);
		CHECK_FALSE(graph.broken.empty());
		CHECK(planDeletion(graph, "textures_src/b.ktx2").Allowed());
	}

	SECTION("restoring the texture does not un-reference it")
	{
		// Blocking is keyed on the edge, not on whether the file happened to exist when we looked.
		WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });

		CHECK_FALSE(planDeletion(root.Scan(), "textures_src/a.ktx2").Allowed());
	}
}

TEST_CASE("A mesh deleted behind the editor's back stops blocking its materials", "[assetrefs]")
{
	// The behaviour a cached graph would get wrong: it would refuse the deletion, naming a mesh that is
	// no longer there. Rebuilding from disk is what makes the answer true rather than merely fresh.
	const DataRoot root("bernini_refs_external_mesh");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/a.ktx2");
	SaveMesh(root, "mesh.bmesh", { "Materials/mat.bmaterial" });

	REQUIRE_FALSE(planDeletion(root.Scan(), "Materials/mat.bmaterial").Allowed());

	fs::remove(root.path / "Meshes" / "mesh.bmesh");

	CHECK(planDeletion(root.Scan(), "Materials/mat.bmaterial").Allowed());
}

TEST_CASE("An asset already gone counts as deleted", "[assetrefs]")
{
	// The user may well have deleted it from a file manager since the scan. That is the outcome they
	// asked for, not a failure to report.
	const DataRoot root("bernini_refs_vanished");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });

	const DeletionPlan plan = planDeletion(root.Scan(), "textures_src/a.ktx2");
	REQUIRE(plan.Allowed());

	fs::remove(root.path / "textures_src" / "a.ktx2");

	CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
}

TEST_CASE("A directory is held only from outside it", "[assetrefs]")
{
	// The rule that makes a folder deletable at all. Everything inside goes together, so an edge with
	// both ends inside is not holding anything back -- only an edge reaching in from outside is.
	const DataRoot root("bernini_refs_dir");

	// A self-contained folder: its material routes from its own texture, and nothing else names either.
	WriteSource(root.path / "textures_src" / "kirk" / "tex0.ktx2", { { 200, 0, 0, 255 } });
	fs::create_directories(root.path / "Materials" / "kirk");
	BakeAndSave(root, "kirk/Body.bmaterial", "textures_src/kirk/tex0.ktx2");

	SECTION("a folder whose references are all internal deletes, and takes them with it")
	{
		// Materials/kirk names a texture *outside* itself, which is an edge pointing out, not in. That
		// texture stays -- the same rule that keeps a deleted mesh's materials.
		const DeletionPlan plan = planDeletion(root.Scan(), "Materials/kirk");

		REQUIRE(plan.Allowed());
		REQUIRE(plan.IsDirectory());
		CHECK(plan.contents == std::vector<std::string>{ "Materials/kirk/Body.bmaterial" });

		REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);

		CHECK_FALSE(fs::exists(root.path / "Materials" / "kirk"));
		CHECK(fs::exists(root.path / "textures_src" / "kirk" / "tex0.ktx2"));
	}

	SECTION("a folder something outside routes from does not")
	{
		const DeletionPlan plan = planDeletion(root.Scan(), "textures_src/kirk");

		REQUIRE_FALSE(plan.Allowed());
		REQUIRE(plan.blockers.size() == 1);
		CHECK(plan.blockers.front().referrer == "Materials/kirk/Body.bmaterial");
		CHECK(plan.blockers.front().kind == RefKind::kChannelRoute);

		CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kRefused);
		CHECK(fs::exists(root.path / "textures_src" / "kirk" / "tex0.ktx2"));
	}

	SECTION("and a folder holding the mesh is never held, because nothing names a mesh")
	{
		SaveMesh(root, "kirk.bmesh", { "Materials/kirk/Body.bmaterial" });

		CHECK(planDeletion(root.Scan(), "Meshes").Allowed());
	}
}

TEST_CASE("Deleting a directory takes every file under it, tracked or not", "[assetrefs]")
{
	// remove_all does not ask what a file is for, so the plan must not either: a README the user dropped
	// in the folder goes with it, and the count it is warned with has to include that.
	const DataRoot root("bernini_refs_dir_contents");

	WriteSource(root.path / "textures_src" / "props" / "tex0.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(
		root.path / "textures_src" / "props" / "nested" / "tex1.ktx2",
		{ { 0, 9, 0, 255 } });
	std::ofstream(root.path / "textures_src" / "props" / "notes.txt") << "source: some_dcc_tool";

	const DeletionPlan plan = planDeletion(root.Scan(), "textures_src/props");

	REQUIRE(plan.Allowed());
	CHECK(
		plan.contents == std::vector<std::string>{ "textures_src/props/nested/tex1.ktx2",
	                                               "textures_src/props/notes.txt",
	                                               "textures_src/props/tex0.ktx2" });

	REQUIRE(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
	CHECK_FALSE(fs::exists(root.path / "textures_src" / "props"));
}

TEST_CASE("A directory is held by a reference into any depth of it", "[assetrefs]")
{
	// Deleting textures_src would take kirk/tex0.ktx2 with it, so the material two levels down still
	// holds the whole tree. A check that only looked at the folder's immediate children would miss it.
	const DataRoot root("bernini_refs_dir_deep");

	WriteSource(root.path / "textures_src" / "kirk" / "tex0.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "textures_src/kirk/tex0.ktx2");

	const DeletionPlan plan = planDeletion(root.Scan(), "textures_src");

	REQUIRE_FALSE(plan.Allowed());
	CHECK(plan.blockers.front().target == "textures_src/kirk/tex0.ktx2");
}

TEST_CASE("The data root itself is not something inside the data root", "[assetrefs]")
{
	const DataRoot root("bernini_refs_dir_escape");

	for (const char* path : { "", ".", "..", "../Meshes" })
	{
		INFO("path: " << path);
		CHECK_THROWS_AS(planDeletion(root.Scan(), path), std::runtime_error);
	}
}

#ifdef _WIN32
TEST_CASE("An asset held open cannot be deleted, and says so", "[assetrefs]")
{
	// Not hypothetical: the editor decodes .ktx2 thumbnails on a thread pool, and Windows will not
	// unlink a file that is open. Reporting a success we did not achieve would leave the user staring
	// at a file they were told was gone.
	const DataRoot root("bernini_refs_locked");

	WriteSource(root.path / "textures_src" / "a.ktx2", { { 200, 0, 0, 255 } });

	const DeletionPlan plan = planDeletion(root.Scan(), "textures_src/a.ktx2");
	REQUIRE(plan.Allowed());

	const fs::path file   = root.path / "textures_src" / "a.ktx2";
	const HANDLE   handle = ::CreateFileW(
		file.wstring().c_str(),
		GENERIC_READ,
		0,  // no sharing: exactly what an in-flight decode holds
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	REQUIRE(handle != INVALID_HANDLE_VALUE);

	const DeletionResult result = deleteAsset(plan, root.Desc());

	::CloseHandle(handle);

	CHECK(result.status == DeletionStatus::kFailed);
	CHECK_FALSE(result.error.empty());
	CHECK(fs::exists(file));

	// And once the reader lets go, the same plan goes through.
	CHECK(deleteAsset(plan, root.Desc()).status == DeletionStatus::kDeleted);
}
#endif

namespace
{
	/**
	 * A whole environment on disk: a `.bsky` and a `.benvl` naming their sources and baked maps, and a
	 * `.benv` composing the pair. The files the routes name are written too, so nothing lands in
	 * `broken` and each edge is judged on the reference rather than on the absence.
	 */
	struct Environment
	{
		std::string env      = "Environments/forest.benv";
		std::string sky      = "Sky/forest.bsky";
		std::string lighting = "EnvLighting/forest.benvl";

		std::string skySource  = "textures_src/forest.ktx2";
		std::string skyBaked   = "Textures/forest_sky.ktx2";
		std::string prefilter  = "Textures/forest_prefilter.ktx2";
		std::string irradiance = "Textures/forest_irradiance.ktx2";
	};

	Environment
	WriteEnvironment(const DataRoot& root)
	{
		const Environment e;

		for (const std::string* dir : { &e.env, &e.sky, &e.lighting })
			fs::create_directories((root.path / *dir).parent_path());
		fs::create_directories(root.path / "Textures");

		for (const std::string* map : { &e.skySource, &e.skyBaked, &e.prefilter, &e.irradiance })
			WriteSource(root.path / *map, { { 10, 20, 30, 255 } });

		BSky sky;
		sky.name       = "forest";
		sky.sky.source = e.skySource;
		sky.sky.baked  = e.skyBaked;
		saveSky(sky, root.path / e.sky);

		BEnvLighting lighting;
		lighting.name              = "forest";
		lighting.prefilter.source  = e.skySource;
		lighting.prefilter.baked   = e.prefilter;
		lighting.irradiance.source = e.skySource;
		lighting.irradiance.baked  = e.irradiance;
		saveEnvLighting(lighting, root.path / e.lighting);

		BEnv env;
		env.name     = "forest";
		env.sky      = e.sky;
		env.lighting = e.lighting;
		saveEnv(env, root.path / e.env);

		return e;
	}
}

// The hole this closes: the graph knew .bmesh, .bmaterial and .ktx2 and nothing else, so a baked
// environment map came back unreferenced and the editor -- which gates deletion on this -- would have
// let a live one go.
TEST_CASE("A baked environment map is held by the container that baked it", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_baked");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.environmentsScanned == 3);
	CHECK(graph.broken.empty());

	for (const std::string* map : { &e.skyBaked, &e.prefilter, &e.irradiance })
	{
		INFO("baked map: " << *map);
		CHECK(graph.IsReferenced(*map));
		CHECK_FALSE(planDeletion(graph, *map).Allowed());
	}

	// Named by the container that wrote it, and reported as such rather than as some other edge.
	const std::span<const AssetRef> holders = graph.ReferrersOf(e.skyBaked);
	REQUIRE(holders.size() == 1);
	CHECK(holders.front().referrer == e.sky);
	CHECK(holders.front().kind == RefKind::kBakedMap);
}

// The source is what a re-bake reads, so it is held for the same reason a material's routed source is.
TEST_CASE("An environment source is held by what bakes from it", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_source");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	CHECK(graph.IsReferenced(e.skySource));
	CHECK_FALSE(planDeletion(graph, e.skySource).Allowed());

	// One source, three routes across two containers -- and the sky and the lighting are both named,
	// each once, because a referrer reported twice would read as two blockers to the user.
	const std::span<const AssetRef> holders = graph.ReferrersOf(e.skySource);
	CHECK(holders.size() == 2);
	CHECK(std::ranges::all_of(holders, [](const AssetRef& ref) {
		return ref.kind == RefKind::kEnvSource;
	}));
}

// A `.benv` holds no pixels, so composing the pair is the only thing it does -- and the only thing
// keeping either half from being deleted out from under it.
TEST_CASE("A .benv holds the sky and the lighting it composes", "[assetrefs]")
{
	const DataRoot    root("bernini_refs_env_parts");
	const Environment e = WriteEnvironment(root);

	const AssetRefGraph graph = root.Scan();

	for (const std::string* part : { &e.sky, &e.lighting })
	{
		INFO("part: " << *part);

		const std::span<const AssetRef> holders = graph.ReferrersOf(*part);
		REQUIRE(holders.size() == 1);
		CHECK(holders.front().referrer == e.env);
		CHECK(holders.front().kind == RefKind::kEnvironmentPart);

		CHECK_FALSE(planDeletion(graph, *part).Allowed());
	}

	// Nothing names the .benv, so it is the one end of the chain that can go -- and taking it leaves
	// the pair behind, exactly as deleting a mesh leaves its materials.
	CHECK(planDeletion(graph, e.env).Allowed());
}

// Before this, planDeletion threw on one: a .bsky was "a file of no kind this project stores anything
// about", so the editor could not even ask the question.
TEST_CASE("The environment containers are assets the project knows", "[assetrefs]")
{
	CHECK(assetTypeFromExtension("Environments/forest.benv") == AssetType::kEnvironment);
	CHECK(assetTypeFromExtension("Sky/forest.bsky") == AssetType::kSky);
	CHECK(assetTypeFromExtension("EnvLighting/forest.benvl") == AssetType::kEnvLighting);

	// The suffix decides, case-insensitively, as it does for every other kind.
	CHECK(assetTypeFromExtension("Sky/FOREST.BSKY") == AssetType::kSky);

	// .benvl must not be read as a .benv with something after it.
	CHECK(assetTypeFromExtension("a.benvl") != AssetType::kEnvironment);
}

// Fatal on purpose, as it is for a mesh or a material: an environment we cannot read is one whose
// maps we cannot see, and we would then delete one out from under it.
TEST_CASE("An unreadable environment stops the scan rather than being skipped", "[assetrefs]")
{
	const DataRoot root("bernini_refs_env_unreadable");
	WriteEnvironment(root);

	std::ofstream(root.path / "Sky" / "broken.bsky", std::ios::binary) << "not a sky";

	REQUIRE_THROWS_AS(root.Scan(), std::runtime_error);
}
