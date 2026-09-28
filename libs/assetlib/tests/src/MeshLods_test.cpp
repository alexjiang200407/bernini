#include "ImportUnitGroup.h"
#include "PointsGltf.h"
#include "SkinnedGltf.h"
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/reimport.h>  // IWYU pragma: keep
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The cook's half of static levels of detail: a source's `<mesh>_LOD<n>` meshes fold into levels of
// `<mesh>`, laid out level-major beside the base's submeshes, each with a threshold the document
// authors or the cook defaults; and every way a source can mis-describe its levels is refused
// rather than cooked into something a renderer draws wrongly.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	/** One glTF mesh: its name, and a triangle count per triangle primitive. */
	struct MeshSpec
	{
		std::string           name;
		std::vector<uint32_t> primitives;
		bool                  grass = false;
	};

	/** A `.glb` holding `meshes`, one node each, every triangle a small fan at the origin. */
	Glb
	MakeGlb(const char* file, const std::vector<MeshSpec>& meshes)
	{
		auto buffer     = Buffer();
		auto gltfMeshes = nlohmann::json::array();
		auto nodes      = nlohmann::json::array();
		auto sceneNodes = nlohmann::json::array();
		for (size_t m = 0; m < meshes.size(); ++m)
		{
			auto primitives = nlohmann::json::array();
			for (const uint32_t triangles : meshes[m].primitives)
			{
				auto positions = std::vector<glm::vec3>();
				for (uint32_t t = 0; t < triangles; ++t)
				{
					const auto x = static_cast<float>(t);
					positions.emplace_back(x, 0.0f, 0.0f);
					positions.emplace_back(x + 1.0f, 0.0f, 0.0f);
					positions.emplace_back(x, 0.0f, 1.0f);
				}
				primitives.push_back(
					{ { "attributes",
				        { { "POSITION", buffer.Add(positions, "VEC3", c_Float) } } } });
			}
			if (meshes[m].grass)
			{
				const std::vector<glm::vec3> points = { glm::vec3(0.0f),
					                                    glm::vec3(1.0f, 0.0f, 0.0f) };
				primitives.push_back(
					{ { "attributes", { { "POSITION", buffer.Add(points, "VEC3", c_Float) } } },
				      { "mode", 0 } });
			}
			gltfMeshes.push_back({ { "name", meshes[m].name }, { "primitives", primitives } });
			nodes.push_back({ { "mesh", m }, { "name", meshes[m].name } });
			sceneNodes.push_back(m);
		}

		auto document           = nlohmann::json::object();
		document["scene"]       = 0;
		document["scenes"]      = { { { "nodes", sceneNodes } } };
		document["nodes"]       = nodes;
		document["meshes"]      = gltfMeshes;
		document["bufferViews"] = buffer.views;
		document["accessors"]   = buffer.accessors;
		return Glb(file, document, buffer);
	}

	std::string_view
	NameOf(const imp::BMeshImport& mesh, const Submesh& submesh)
	{
		return mesh.stringPool.at(submesh.nameOffset);
	}

	std::vector<float>
	TableOf(const std::vector<MeshLod>& lods, const Mesh& entry)
	{
		auto table = std::vector<float>();
		for (uint32_t level = 0; level < entry.lodCount; ++level)
			table.push_back(lods[entry.firstLod + level].minPixels);
		return table;
	}

	/** Triangles in submesh `s` of level `level`: which source primitive landed in that entry. */
	uint32_t
	TrianglesAt(const imp::BMeshImport& mesh, const Mesh& entry, uint32_t level, uint32_t s)
	{
		return mesh.submeshes[entry.firstSubmesh + level * entry.submeshCount + s].vertexCount / 3;
	}
}

TEST_CASE("a source's _LOD meshes fold into levels of their base, level-major", "[lod][gltf]")
{
	// Two materials on the tree, so the order inside a level is checked as well as between them.
	const Glb glb = MakeGlb(
		"bernini_lod_fold.glb",
		{ { "Tree", { 8, 6 } },
	      { "Rock", { 4 } },
	      { "Tree_LOD2", { 2, 1 } },
	      { "Tree_LOD1", { 4, 3 } } });

	const imp::BMeshImport mesh = loadFromGltf(glb.Path());

	REQUIRE(mesh.meshes.size() == 2);
	const Mesh& tree = mesh.meshes[0];
	const Mesh& rock = mesh.meshes[1];

	CHECK(mesh.stringPool.at(tree.nameOffset) == "Tree");
	CHECK(tree.lodCount == 3);
	CHECK(tree.submeshCount == 2);
	REQUIRE(mesh.submeshes.size() == 3 * 2 + 1);

	// Level-major, whatever order the source listed the levels in.
	CHECK(TrianglesAt(mesh, tree, 0, 0) == 8);
	CHECK(TrianglesAt(mesh, tree, 0, 1) == 6);
	CHECK(TrianglesAt(mesh, tree, 1, 0) == 4);
	CHECK(TrianglesAt(mesh, tree, 1, 1) == 3);
	CHECK(TrianglesAt(mesh, tree, 2, 0) == 2);
	CHECK(TrianglesAt(mesh, tree, 2, 1) == 1);

	// A level answers to its level-0 sibling's name, which is what a binding addresses.
	for (uint32_t level = 1; level < tree.lodCount; ++level)
	{
		CHECK(NameOf(mesh, mesh.submeshes[tree.firstSubmesh + level * 2]) == "Tree[0]");
		CHECK(NameOf(mesh, mesh.submeshes[tree.firstSubmesh + level * 2 + 1]) == "Tree[1]");
	}

	SECTION("every mesh gets a table once one has levels, defaulted")
	{
		CHECK(TableOf(mesh.lods, tree) == std::vector<float>{ 160.0f, 80.0f, 0.0f });
		CHECK(rock.lodCount == 1);
		CHECK(TableOf(mesh.lods, rock) == std::vector<float>{ 0.0f });
	}

	SECTION("a level's node places nothing, and the others name their folded mesh")
	{
		REQUIRE(mesh.nodes.size() == 4);
		CHECK(mesh.nodes[0].mesh == 0);
		CHECK(mesh.nodes[1].mesh == 1);
		CHECK(mesh.nodes[2].mesh == c_InvalidIndex);
		CHECK(mesh.nodes[3].mesh == c_InvalidIndex);
	}

	SECTION("the names a document binds are level 0's, once each")
	{
		BMesh cooked = toBMesh(mesh);
		CHECK_NOTHROW(requireUniqueSubmeshNames(cooked));
	}
}

TEST_CASE("a cooked mesh answers for one level of one of its meshes", "[lod]")
{
	const Glb glb = MakeGlb(
		"bernini_lod_query.glb",
		{ { "Tree", { 8, 6 } }, { "Rock", { 4 } }, { "Tree_LOD1", { 4, 3 } } });
	const BMesh cooked = toBMesh(loadFromGltf(glb.Path()));
	REQUIRE(cooked.meshes.size() == 2);

	const auto level1 = meshLodSubmeshes(cooked, 0, 1);
	REQUIRE(level1.size() == 2);
	CHECK(level1[0].vertexCount / 3 == 4);
	CHECK(level1[1].vertexCount / 3 == 3);
	CHECK(meshLodSubmeshes(cooked, 1, 0).size() == 1);

	CHECK(meshLodMinPixels(cooked, 0) == std::vector<float>{ 160.0f, 0.0f });
	CHECK(meshLodMinPixels(cooked, 1) == std::vector<float>{ 0.0f });

	SECTION("a level or a mesh that is not there is nothing, not a throw")
	{
		CHECK(meshLodSubmeshes(cooked, 0, 2).empty());
		CHECK(meshLodSubmeshes(cooked, 1, 1).empty());
		CHECK(meshLodSubmeshes(cooked, 2, 0).empty());
		CHECK(meshLodMinPixels(cooked, 2).empty());
	}

	SECTION("every mesh's level together is each one's")
	{
		const std::vector<Submesh> all = lodNSubmeshes(cooked, 1);
		REQUIRE(all.size() == 2);
		CHECK(all[0].vertexCount == level1[0].vertexCount);
	}

	SECTION("a container with no table is one level drawn at every size")
	{
		const Glb   crate = MakeGlb("bernini_lod_query_none.glb", { { "Crate", { 2 } } });
		const BMesh plain = toBMesh(loadFromGltf(crate.Path()));
		REQUIRE(plain.lods.empty());
		CHECK(meshLodMinPixels(plain, 0) == std::vector<float>{ 0.0f });
		CHECK(meshLodSubmeshes(plain, 0, 0).size() == 1);
	}
}

TEST_CASE("a source with no levels cooks as it did before them", "[lod][gltf]")
{
	const Glb glb = MakeGlb("bernini_lod_none.glb", { { "Crate", { 2 } }, { "Barrel", { 3 } } });

	const imp::BMeshImport mesh = loadFromGltf(glb.Path());

	REQUIRE(mesh.meshes.size() == 2);
	CHECK(mesh.meshes[0].lodCount == 1);
	CHECK(mesh.meshes[1].lodCount == 1);
	CHECK(mesh.lods.empty());
}

TEST_CASE("a base may be named as its own level 0", "[lod][gltf]")
{
	const Glb glb =
		MakeGlb("bernini_lod_zero.glb", { { "Bush_LOD0", { 4 } }, { "Bush_LOD1", { 2 } } });

	const imp::BMeshImport mesh = loadFromGltf(glb.Path());

	REQUIRE(mesh.meshes.size() == 1);
	CHECK(mesh.meshes[0].lodCount == 2);
	CHECK(mesh.stringPool.at(mesh.meshes[0].nameOffset) == "Bush_LOD0");
}

TEST_CASE("authored thresholds replace the defaults where the list reaches", "[lod][gltf]")
{
	const Glb glb = MakeGlb(
		"bernini_lod_authored.glb",
		{ { "Tree", { 8 } }, { "Tree_LOD1", { 4 } }, { "Tree_LOD2", { 2 } } });

	SECTION("every level")
	{
		const imp::BMeshImport mesh =
			loadFromGltf(glb.Path(), { .lodMinPixels = { 100.0f, 30.0f, 5.0f } });
		CHECK(TableOf(mesh.lods, mesh.meshes[0]) == std::vector<float>{ 100.0f, 30.0f, 5.0f });
	}

	SECTION("a short list, the rest defaulted")
	{
		const imp::BMeshImport mesh = loadFromGltf(glb.Path(), { .lodMinPixels = { 200.0f } });
		CHECK(TableOf(mesh.lods, mesh.meshes[0]) == std::vector<float>{ 200.0f, 80.0f, 0.0f });
	}

	SECTION("a single-level mesh takes a draw-nothing size")
	{
		const Glb              rock = MakeGlb("bernini_lod_rock.glb", { { "Rock", { 4 } } });
		const imp::BMeshImport mesh = loadFromGltf(rock.Path(), { .lodMinPixels = { 4.0f } });
		CHECK(TableOf(mesh.lods, mesh.meshes[0]) == std::vector<float>{ 4.0f });
	}
}

TEST_CASE("a source that mis-describes its levels is refused, with its reason", "[lod][gltf]")
{
	using Catch::Matchers::ContainsSubstring;

	const auto refuses = [](const char*                  file,
	                        const std::vector<MeshSpec>& meshes,
	                        std::vector<float>           authored,
	                        std::string_view             reason) {
		const Glb glb = MakeGlb(file, meshes);
		CHECK_THROWS_WITH(
			loadFromGltf(glb.Path(), { .lodMinPixels = std::move(authored) }),
			ContainsSubstring(std::string(reason)));
	};

	SECTION("a level of a mesh the source does not have")
	{
		refuses("bernini_lod_orphan.glb", { { "Tree_LOD1", { 2 } } }, {}, "does not have");
	}
	SECTION("a base named both ways")
	{
		refuses(
			"bernini_lod_twins.glb",
			{ { "Tree", { 2 } }, { "Tree_LOD0", { 2 } } },
			{},
			"two meshes are level 0");
	}
	SECTION("a gap in the levels")
	{
		refuses(
			"bernini_lod_gap.glb",
			{ { "Tree", { 4 } }, { "Tree_LOD1", { 2 } }, { "Tree_LOD3", { 1 } } },
			{},
			"no level 2");
	}
	SECTION("more levels than a mesh may carry")
	{
		auto meshes = std::vector<MeshSpec>{ { "Tree", { 1 } } };
		for (uint32_t level = 1; level <= c_MaxMeshLods; ++level)
			meshes.push_back({ "Tree_LOD" + std::to_string(level), { 1 } });
		refuses("bernini_lod_many.glb", meshes, {}, "more than the 8");
	}
	SECTION("a level with another number of materials")
	{
		refuses(
			"bernini_lod_prims.glb",
			{ { "Tree", { 4, 4 } }, { "Tree_LOD1", { 2 } } },
			{},
			"triangle primitives");
	}
	SECTION("a level skinned differently from its base")
	{
		// The rig's body, and a level of it with the same triangle and no joints.
		const SkinnedGltf rig(
			"bernini_lod_skin",
			{ { R"("indices": 3, "mode": 4 } ] } ],)",
		        R"("indices": 3, "mode": 4 } ] },
			    { "name": "body_LOD1", "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 3 } ] } ],)" },
		      { R"({ "name": "spine", "translation": [ 0, 2, 0 ] })",
		        R"({ "name": "spine", "translation": [ 0, 2, 0 ] },
			    { "mesh": 1, "name": "body_LOD1" })" } });
		CHECK_THROWS_WITH(loadFromGltf(rig.PackGlb()), ContainsSubstring("skinned differently"));
	}
	SECTION("a level carrying grass")
	{
		refuses(
			"bernini_lod_grass.glb",
			{ { "Tree", { 4 } }, { "Tree_LOD1", { 2 }, true } },
			{},
			"carries grass");
	}
	SECTION("a list longer than a mesh's levels")
	{
		refuses(
			"bernini_lod_long.glb",
			{ { "Tree", { 4 } }, { "Tree_LOD1", { 2 } }, { "Rock", { 2 } } },
			{ 100.0f, 10.0f },
			"lists 2 levels, but mesh 'Rock' carries 1");
	}
	SECTION("authored entries the defaults after them would rise past")
	{
		refuses(
			"bernini_lod_rise.glb",
			{ { "Tree", { 4 } }, { "Tree_LOD1", { 2 } }, { "Tree_LOD2", { 1 } } },
			{ 20.0f },
			"author every level");
	}
}

namespace
{
	struct LodProject
	{
		Project  project;
		fs::path dataRoot;

		explicit LodProject(const char* name, const fs::path& glb) : project(Make(name))
		{
			dataRoot = project.GetDataDirectory();
			ImportUnitGroup(dataRoot, glb);
			project.ReloadStore();
		}

		/** Rewrites the group's document with `lodMinPixels` authored, as a person would. */
		void
		Author(std::vector<float> lodMinPixels) const
		{
			const fs::path document = dataRoot / c_MeshSourcesDirectoryName / "unit.bimport";
			ImportDocument authored = loadImportDocument(document);
			authored.lodMinPixels   = std::move(lodMinPixels);
			core::file::write_atomic(document, AssetCodec<ImportDocument>::Serialize(authored));
		}

		[[nodiscard]] BMesh
		Mesh() const
		{
			return AssetCodec<BMesh>::Deserialize(
				core::file::read_file_bytes((dataRoot / "Derived/Meshes/unit.bmesh").string()));
		}

	private:
		static Project
		Make(const char* name)
		{
			const fs::path root = fs::temp_directory_path() / name;
			fs::remove_all(root);
			return Project::Create(root / "Lods.bproj", "Lods");
		}
	};
}

TEST_CASE(
	"a document's thresholds reach the mesh by every path that writes one",
	"[lod][importdoc]")
{
	const Glb glb = MakeGlb(
		"bernini_lod_project.glb",
		{ { "Tree", { 8 } }, { "Tree_LOD1", { 4 } }, { "Tree_LOD2", { 2 } } });

	LodProject lods("bernini_lod_project", glb.Path());
	REQUIRE(
		TableOf(lods.Mesh().lods, lods.Mesh().meshes[0]) ==
		std::vector<float>{ 160.0f, 80.0f, 0.0f });

	lods.Author({ 100.0f, 30.0f, 5.0f });

	SECTION("a mesh put back from its document")
	{
		fs::remove(lods.dataRoot / "Derived/Meshes/unit.bmesh");
		lods.project.ReloadStore();
		static_cast<void>(lods.project.GetStore().Reimport(/*dryRun*/ false));

		CHECK(
			TableOf(lods.Mesh().lods, lods.Mesh().meshes[0]) ==
			std::vector<float>{ 100.0f, 30.0f, 5.0f });
	}

	SECTION("a re-import of the source, which keeps what the document authors")
	{
		ImportUnitGroup(lods.dataRoot, glb.Path());

		const BMesh mesh = lods.Mesh();
		CHECK(TableOf(mesh.lods, mesh.meshes[0]) == std::vector<float>{ 100.0f, 30.0f, 5.0f });

		// And the key the import recorded agrees with the document, so the mesh reads current.
		const ImportDocument document =
			loadImportDocument(lods.dataRoot / c_MeshSourcesDirectoryName / "unit.bimport");
		CHECK(document.lodMinPixels == std::vector<float>{ 100.0f, 30.0f, 5.0f });
		CHECK(mesh.source.parametersHash == parametersHashOf(document));
	}
}
