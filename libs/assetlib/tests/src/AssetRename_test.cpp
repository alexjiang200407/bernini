#include <array>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/material_bake.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>

#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "RefsSandbox.h"

#include "MountAt.h"
#include "mounted_io.h"
#include <assetlib/project_layout.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/VertexLayout.h>

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	/** Plans and executes the rename in one step, for the tests about the outcome rather than the plan. */
	RenameResult
	Rename(const DataRoot& root, std::string_view from, std::string_view to)
	{
		return root.Source().RenameAsset(planRename(root.Scan(), from, to));
	}

	/** The smallest real rig: one bone at the bind pose. */
	Skeleton
	MakeRig()
	{
		auto skeleton   = Skeleton();
		auto bone       = Bone();
		bone.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		bone.parent     = c_InvalidIndex;
		bone.nameOffset = skeleton.stringPool.add("root");
		skeleton.bones.push_back(bone);
		skeleton.bones[0].inverseBind = glm::inverse(bindPoseModelTransforms(skeleton)[0]);
		return skeleton;
	}

	/** Every key one import writes, so no test re-spells a category. */
	struct Import
	{
		std::string source;
		std::string document;
		std::string mesh;
		std::string skeleton;  // empty for a source carrying no skin
		std::string animations;
	};

	/**
	 * A whole import on disk under `stem`: the `.glb`, the containers it produced, and the
	 * `.bimport` naming them in `outputs`. `rigged` is the difference between the two shapes an
	 * import has -- three containers, or a mesh alone.
	 */
	Import
	WriteImport(const DataRoot& root, const std::string& stem, bool rigged)
	{
		auto out       = Import();
		out.source     = "Authored/Meshes/" + stem + ".glb";
		out.document   = "Authored/Meshes/" + stem + ".bimport";
		out.mesh       = "Derived/Meshes/" + stem + ".bmesh";
		out.skeleton   = rigged ? "Derived/Skeletons/" + stem + ".bskel" : std::string();
		out.animations = rigged ? "Derived/Animations/" + stem + ".banim" : std::string();

		auto document    = ImportDocument();
		document.source  = out.source;
		document.outputs = { out.mesh };

		if (rigged)
		{
			const Skeleton rig = MakeRig();
			fs::create_directories(root.path / "Derived/Skeletons");
			StoreAt(root.path).Save(rig, out.skeleton);

			auto animations              = AnimationSet();
			animations.skeleton          = out.skeleton;
			animations.skeletonSignature = skeletonSignature(rig);
			animations.boneCount         = 1;

			auto clip       = AnimationClip();
			clip.nameOffset = animations.stringPool.add("rest");
			clip.frameCount = 1;
			clip.sampleRate = 30.0f;
			animations.clips.push_back(clip);
			animations.samples.push_back(rig.bones[0].bindPose);

			fs::create_directories(root.path / "Derived/Animations");
			StoreAt(root.path).Save(animations, out.animations);

			document.skeleton = out.skeleton;
			document.outputs  = { out.animations, out.mesh, out.skeleton };
		}

		SaveMesh(root, (stem + ".bmesh").c_str(), {}, out.skeleton);

		fs::create_directories(root.path / "Authored/Meshes");
		core::file::write_atomic(
			root.path / out.document,
			AssetCodec<ImportDocument>::Serialize(document));
		std::ofstream(root.path / out.source) << "source";

		return out;
	}
}

TEST_CASE("Renaming a texture rewrites the graph that compiles into its routes", "[assetrename]")
{
	// The editor compiles `editorGraph` back into `routes`, so a graph left naming the old file
	// quietly undoes the rename the next time anyone opens the material -- routes and graph
	// disagree, the graph wins, and the material ends up pointing at a file that is gone.
	const DataRoot root("bernini_rename_material_graph");
	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 255, 0, 0, 255 } });

	BMaterial material   = BakeAndSave(root, "m.bmaterial", "Authored/Textures/a.ktx2");
	material.editorGraph = R"({"nodes":[{"id":1,"internal-data":{"model-name":"Texture",)"
						   R"("texture":"Authored/Textures/a.ktx2"}}]})";
	StoreAt(root.path).Save(material, "Authored/Materials/m.bmaterial");

	REQUIRE(
		Rename(root, "Authored/Textures/a.ktx2", "Authored/Textures/b.ktx2").status ==
		RenameStatus::kRenamed);

	const BMaterial after = StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial");
	CHECK(after.pbr.routes[0].texture == "Authored/Textures/b.ktx2");
	CHECK(after.editorGraph.find("Authored/Textures/b.ktx2") != std::string::npos);
	CHECK(after.editorGraph.find("Authored/Textures/a.ktx2") == std::string::npos);
}

TEST_CASE("Scanning a material with a real board terminates", "[assetrename]")
{
	// A board is mostly numbers -- node ids, port indices, positions. Iterating a primitive in
	// nlohmann yields the value itself, so a walk that recurses into one never returns: the scan
	// blew the stack, and every caller of it went down with it, the editor opening a project
	// included. There is no assertion to make beyond arriving here.
	const DataRoot root("bernini_refs_material_graph_scalars");
	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 255, 255, 0, 255 } });

	BMaterial material   = BakeAndSave(root, "m.bmaterial", "Authored/Textures/a.ktx2");
	material.editorGraph = R"({"connections":[{"inNodeId":0,"inPortIndex":2,"outNodeId":2}],)"
						   R"("nodes":[{"id":0,"internal-data":{"baseColorA":1,"split":)"
						   R"([false,false,false],"model-name":"MaterialOutput"},)"
						   R"("position":{"x":220,"y":40}},{"id":1,"internal-data":)"
						   R"({"model-name":"Texture","texture":"Authored/Textures/a.ktx2"},)"
						   R"("position":{"x":-160,"y":0}}],"nulls":[null]})";
	StoreAt(root.path).Save(material, "Authored/Materials/m.bmaterial");

	CHECK(root.Scan().IsReferenced("Authored/Textures/a.ktx2"));
}

TEST_CASE("A texture only the graph names is still referenced", "[assetrename]")
{
	// An unconnected texture node holds a file alive as surely as a wired one: it is authoring the
	// user can see and re-wire. Seeing only `routes` would let the file be deleted out from under a
	// board nobody is looking at.
	const DataRoot root("bernini_rename_material_graph_only");
	WriteSource(root.path / "Authored/Textures" / "wired.ktx2", { { 255, 0, 0, 255 } });
	WriteSource(root.path / "Authored/Textures" / "loose.ktx2", { { 0, 0, 255, 255 } });

	BMaterial material   = BakeAndSave(root, "m.bmaterial", "Authored/Textures/wired.ktx2");
	material.editorGraph = R"({"nodes":[{"id":1,"internal-data":{"model-name":"Texture",)"
						   R"("texture":"Authored/Textures/loose.ktx2"}}]})";
	StoreAt(root.path).Save(material, "Authored/Materials/m.bmaterial");

	const AssetRefGraph graph = root.Scan();
	CHECK(graph.IsReferenced("Authored/Textures/wired.ktx2"));
	CHECK(graph.IsReferenced("Authored/Textures/loose.ktx2"));

	SECTION("and moves with it")
	{
		REQUIRE(
			Rename(root, "Authored/Textures/loose.ktx2", "Authored/Textures/moved.ktx2").status ==
			RenameStatus::kRenamed);

		const std::string after =
			StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial").editorGraph;
		CHECK(after.find("Authored/Textures/moved.ktx2") != std::string::npos);
		CHECK(after.find("Authored/Textures/loose.ktx2") == std::string::npos);
	}
}

TEST_CASE("A rename changes only the key it moves in the board", "[assetrename]")
{
	// The board's formatting is the editor's -- key order, spacing, how it spells a double. Parsing
	// it and writing it back out here would impose a second JSON writer's spelling on it, and the
	// two would then take turns rewriting the file on every rename and every save. Only the key
	// moves.
	const DataRoot root("bernini_rename_material_graph_bytes");
	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 12, 34, 56, 255 } });

	const std::string graph =
		R"({"nodes":[{"id":1, "zz":1.5, "aa":[false,null],)"
		R"("internal-data":{"model-name":"Texture","texture":"Authored/Textures/a.ktx2"}}]})";

	BMaterial material   = BakeAndSave(root, "m.bmaterial", "Authored/Textures/a.ktx2");
	material.editorGraph = graph;
	StoreAt(root.path).Save(material, "Authored/Materials/m.bmaterial");

	REQUIRE(
		Rename(root, "Authored/Textures/a.ktx2", "Authored/Textures/b.ktx2").status ==
		RenameStatus::kRenamed);

	std::string expected = graph;
	expected.replace(
		expected.find("Authored/Textures/a.ktx2"),
		std::string_view("Authored/Textures/a.ktx2").size(),
		"Authored/Textures/b.ktx2");

	// Byte for byte: the odd key order, the trailing `.5`, the spaces and the null all survive.
	CHECK(
		StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial").editorGraph ==
		expected);
}

TEST_CASE("A graph that will not parse survives a rename unchanged", "[assetrename]")
{
	// Its schema is the editor's and this knows none of it. Text that is not JSON at all is left
	// exactly as it stands rather than dropped, which would lose the node layout.
	const DataRoot root("bernini_rename_material_graph_bad");
	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 0, 255, 0, 255 } });

	BMaterial material   = BakeAndSave(root, "m.bmaterial", "Authored/Textures/a.ktx2");
	material.editorGraph = "not json at all";
	StoreAt(root.path).Save(material, "Authored/Materials/m.bmaterial");

	REQUIRE(
		Rename(root, "Authored/Textures/a.ktx2", "Authored/Textures/b.ktx2").status ==
		RenameStatus::kRenamed);

	CHECK(
		StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial").editorGraph ==
		"not json at all");
}

TEST_CASE("Renaming an unreferenced asset moves the file", "[assetrename]")
{
	const DataRoot root("bernini_rename_loose");

	WriteSource(root.path / "Authored/Textures" / "old.ktx2", { { 200, 0, 0, 255 } });

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Textures/old.ktx2", "Authored/Textures/new.ktx2");

	CHECK(plan.assetType == AssetType::kTexture);
	CHECK(plan.referrers.empty());

	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "Authored/Textures" / "old.ktx2"));
	CHECK(fs::exists(root.path / "Authored/Textures" / "new.ktx2"));
}

TEST_CASE("Renaming a material re-points every mesh that names it", "[assetrename]")
{
	// The rule the feature exists for: a rename is never blocked by references, because the references
	// follow. A mesh left naming the old path would be exactly the broken edge deletion works to prevent.
	const DataRoot root("bernini_rename_material");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "old.bmaterial", "Authored/Textures/a.ktx2");

	SaveMesh(root, "one.bmesh", { "Authored/Materials/old.bmaterial" });
	SaveMesh(
		root,
		"two.bmesh",
		{ "Authored/Materials/other.bmaterial", "Authored/Materials/old.bmaterial" });

	const RenamePlan plan = planRename(
		root.Scan(),
		"Authored/Materials/old.bmaterial",
		"Authored/Materials/new.bmaterial");

	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "Authored/Materials" / "old.bmaterial"));
	CHECK(fs::exists(root.path / "Authored/Materials" / "new.bmaterial"));

	CHECK(
		root.Source().LoadRegenMeshRefs("Derived/Meshes/one.bmesh").materials ==
		std::vector<std::string>{ "Authored/Materials/new.bmaterial" });

	// Only the slot that named it moves; the sibling is untouched.
	CHECK(
		root.Source().LoadRegenMeshRefs("Derived/Meshes/two.bmesh").materials ==
		std::vector<std::string>{ "Authored/Materials/other.bmaterial",
	                              "Authored/Materials/new.bmaterial" });

	SECTION("and the rewritten project has no reference to the old name")
	{
		const AssetRefGraph after = root.Scan();

		CHECK_FALSE(after.IsReferenced("Authored/Materials/old.bmaterial"));
		CHECK(ReferrerPaths(after, "Authored/Materials/new.bmaterial").size() == 4);
	}
}

TEST_CASE("A rewritten mesh still carries its geometry", "[assetrename]")
{
	// The material chunk is a few hundred bytes of a file that is mostly vertex data, and the rewrite
	// round-trips the whole container -- so what must not change is everything else.
	const DataRoot root("bernini_rename_roundtrip");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "old.bmaterial", "Authored/Textures/a.ktx2");
	SaveMesh(
		root,
		"mesh.bmesh",
		{ "Authored/Materials/old.bmaterial", "Authored/Materials/keep.bmaterial" });

	const BMesh before = StoreAt(root.path).Load<BMesh>("Derived/Meshes/mesh.bmesh");

	REQUIRE(
		Rename(root, "Authored/Materials/old.bmaterial", "Authored/Materials/new.bmaterial")
			.status == RenameStatus::kRenamed);

	const BMesh after = StoreAt(root.path).Load<BMesh>("Derived/Meshes/mesh.bmesh");

	CHECK(
		root.Source().LoadRegenMeshRefs("Derived/Meshes/mesh.bmesh").materials ==
		std::vector<std::string>{ "Authored/Materials/new.bmaterial",
	                              "Authored/Materials/keep.bmaterial" });
	CHECK(after.nodes.size() == before.nodes.size());
	CHECK(after.submeshes.size() == before.submeshes.size());
	CHECK(after.stringPool == before.stringPool);
}

TEST_CASE("Renaming a texture re-points the material that routes it", "[assetrename]")
{
	// A material names its textures twice -- the routes a re-bake reads and the triplet its last bake
	// wrote -- and the rename must catch whichever of them names the file.
	const DataRoot root("bernini_rename_texture");

	WriteSource(root.path / "Authored/Textures" / "old.ktx2", { { 200, 0, 0, 255 } });
	const BMaterial baked = BakeAndSave(root, "mat.bmaterial", "Authored/Textures/old.ktx2");

	SECTION("a routed source")
	{
		REQUIRE(
			Rename(root, "Authored/Textures/old.ktx2", "Authored/Textures/new.ktx2").status ==
			RenameStatus::kRenamed);

		const BMaterial material =
			StoreAt(root.path).Load<BMaterial>("Authored/Materials/mat.bmaterial");

		CHECK(material.pbr.routes[0].texture == "Authored/Textures/new.ktx2");
		CHECK(root.Scan().broken.empty());
	}

	SECTION("a baked map cannot move")
	{
		CHECK_THROWS(planRename(
			root.Scan(),
			bakedTextureKey(baked.pbr.baseColorTexture),
			"Derived/BakedTextures/renamed_basecolor.ktx2"));
	}
}

TEST_CASE("Renaming a texture re-points the material's geometry occlusion map", "[assetrename]")
{
	const DataRoot root("bernini_rename_geometry_occlusion");

	WriteSource(root.path / "Authored/Textures" / "ao.ktx2", { { 128, 128, 128, 255 } });

	BMaterial material;
	material.pbr.geometryOcclusionTexture = "Authored/Textures/ao.ktx2";
	StoreAt(root.path).Save(material, "Authored/Materials/mat.bmaterial");

	REQUIRE(
		Rename(root, "Authored/Textures/ao.ktx2", "Authored/Textures/wall_ao.ktx2").status ==
		RenameStatus::kRenamed);

	CHECK(
		StoreAt(root.path)
			.Load<BMaterial>("Authored/Materials/mat.bmaterial")
			.pbr.geometryOcclusionTexture == "Authored/Textures/wall_ao.ktx2");
	CHECK(root.Scan().broken.empty());
}

TEST_CASE("Derived environment parts cannot move independently", "[assetrename]")
{
	const DataRoot    root("bernini_rename_env");
	const Environment e = WriteEnvironment(root);
	CHECK_THROWS(planRename(root.Scan(), e.sky, "Derived/Sky/dawn.bsky"));
	CHECK_THROWS(planRename(root.Scan(), e.lighting, "Derived/EnvLighting/dawn.benvl"));
	CHECK(StoreAt(root.path).Load<BEnv>(e.env).sky == e.sky);
}

TEST_CASE("Renaming a directory re-points every reference into it", "[assetrename]")
{
	const DataRoot root("bernini_rename_dir");

	// Two referrers of different kinds, both outside: the plan collects an edge by where its
	// *target* sits, so one directory rename has to re-point every kind that reached into it. The
	// case below covers a referrer that is itself inside.
	WriteSource(root.path / "Authored/Textures" / "kirk" / "tex0.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "Authored/Textures" / "kirk" / "tex1.ktx2", { { 0, 200, 0, 255 } });
	BakeAndSave(root, "outside.bmaterial", "Authored/Textures/kirk/tex0.ktx2");

	BSky sky;
	sky.name       = "dusk";
	sky.sky.source = "Authored/Textures/kirk/tex1.ktx2";
	StoreAt(root.path).Save(sky, KeyIn(c_SkyDirectoryName, "dusk.bsky"));

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Textures/kirk", "Authored/Textures/spock");

	CHECK(plan.IsDirectory());

	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	CHECK_FALSE(fs::exists(root.path / "Authored/Textures" / "kirk"));
	CHECK(fs::exists(root.path / "Authored/Textures" / "spock" / "tex0.ktx2"));

	CHECK(
		StoreAt(root.path)
			.Load<BMaterial>("Authored/Materials/outside.bmaterial")
			.pbr.routes[0]
			.texture == "Authored/Textures/spock/tex0.ktx2");
	CHECK(
		StoreAt(root.path).Load<BSky>(KeyIn(c_SkyDirectoryName, "dusk.bsky")).sky.source ==
		"Authored/Textures/spock/tex1.ktx2");

	CHECK(root.Scan().broken.empty());
}

// A `.bimport` and the source it names sit in one directory under one stem, so they are the first
// pair where a referrer and its target both move in a directory rename -- the referrer rewritten and
// the file relocated by the same plan. Every other stored edge crosses a category.
TEST_CASE("Renaming a directory of sources re-points the documents inside it", "[assetrename]")
{
	const DataRoot root("bernini_rename_dir_sources");

	auto document   = ImportDocument();
	document.source = "Authored/Meshes/crew/kirk.glb";
	fs::create_directories(root.path / "Authored/Meshes/crew");
	std::ofstream(root.path / "Authored/Meshes/crew/kirk.glb") << "source";
	core::file::write_atomic(
		root.path / "Authored/Meshes/crew/kirk.bimport",
		AssetCodec<ImportDocument>::Serialize(document));

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Meshes/crew", "Authored/Meshes/bridge");
	REQUIRE(plan.IsDirectory());
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	CHECK(fs::exists(root.path / "Authored/Meshes/bridge/kirk.glb"));
	CHECK(
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/bridge/kirk.bimport")
			.source == "Authored/Meshes/bridge/kirk.glb");

	CHECK(root.Scan().broken.empty());
}

TEST_CASE("planRename refuses what a rename must never do", "[assetrename]")
{
	const DataRoot root("bernini_rename_refuse");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });
	WriteSource(root.path / "Authored/Textures" / "b.ktx2", { { 0, 200, 0, 255 } });

	const AssetRefGraph graph = root.Scan();

	SECTION("changing what kind of asset a file is")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "Authored/Textures/a.bmaterial"),
			std::runtime_error);
	}

	SECTION("overwriting a file that already exists")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "Authored/Textures/b.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming to the name it already has")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "Authored/Textures/a.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming something that does not exist")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/ghost.ktx2", "Authored/Textures/new.ktx2"),
			std::runtime_error);
	}

	SECTION("renaming a file of no kind the project tracks")
	{
		std::ofstream(root.path / "notes.txt") << "hello";
		CHECK_THROWS_AS(planRename(graph, "notes.txt", "notes2.txt"), std::runtime_error);
	}

	SECTION("renaming into a directory that does not exist")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "Authored/Textures/ghost/a.ktx2"),
			std::runtime_error);
	}

	SECTION("moving a directory inside itself")
	{
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures", "Authored/Textures/inner"),
			std::runtime_error);
	}

	SECTION("reaching outside the data root, from either end")
	{
		CHECK_THROWS_AS(
			planRename(graph, "../a.ktx2", "Authored/Textures/a.ktx2"),
			std::runtime_error);
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "../a.ktx2"),
			std::runtime_error);

		// An absolute path is not "inside" anything: operator/ would let it replace the root outright.
		CHECK_THROWS_AS(
			planRename(graph, "Authored/Textures/a.ktx2", "/outside/a.ktx2"),
			std::runtime_error);
		CHECK_THROWS_AS(
			planRename(graph, "/outside/a.ktx2", "Authored/Textures/a.ktx2"),
			std::runtime_error);
	}
}

TEST_CASE("A referrer that stopped parsing fails the rename, and is not touched", "[assetrename]")
{
	// The scan reads a referrer once, at plan time; by execution it may be locked, gone, or corrupted
	// behind the editor's back. That is weather, not a crash: the rename reports kFailed with nothing
	// moved and nothing rewritten.
	const DataRoot root("bernini_rename_badreferrer");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Authored/Textures/a.ktx2");

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Textures/a.ktx2", "Authored/Textures/new.ktx2");

	std::ofstream(root.path / "Authored/Materials" / "mat.bmaterial", std::ios::binary)
		<< "not a material";

	const RenameResult result = root.Source().RenameAsset(plan);

	CHECK(result.status == RenameStatus::kFailed);
	CHECK_FALSE(result.error.empty());
	CHECK(fs::exists(root.path / "Authored/Textures" / "a.ktx2"));
	CHECK_FALSE(fs::exists(root.path / "Authored/Textures" / "new.ktx2"));
}

TEST_CASE("A rename whose file vanished fails without touching the referrers", "[assetrename]")
{
	// The data root is shared with the user's file manager, and a deletion shrugs at a file already
	// gone -- but a rename cannot: rewriting the referrers with nothing to move would break every one.
	const DataRoot root("bernini_rename_vanished");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "Authored/Textures/a.ktx2");

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Textures/a.ktx2", "Authored/Textures/new.ktx2");

	fs::remove(root.path / "Authored/Textures" / "a.ktx2");

	const RenameResult result = root.Source().RenameAsset(plan);

	CHECK(result.status == RenameStatus::kFailed);
	CHECK_FALSE(result.error.empty());

	// The material still says what it said.
	CHECK(
		StoreAt(root.path)
			.Load<BMaterial>("Authored/Materials/mat.bmaterial")
			.pbr.routes[0]
			.texture == "Authored/Textures/a.ktx2");
}

TEST_CASE("A destination taken since the plan fails the rename", "[assetrename]")
{
	const DataRoot root("bernini_rename_taken");

	WriteSource(root.path / "Authored/Textures" / "a.ktx2", { { 200, 0, 0, 255 } });

	const RenamePlan plan =
		planRename(root.Scan(), "Authored/Textures/a.ktx2", "Authored/Textures/new.ktx2");

	WriteSource(root.path / "Authored/Textures" / "new.ktx2", { { 0, 200, 0, 255 } });

	CHECK(root.Source().RenameAsset(plan).status == RenameStatus::kFailed);
	CHECK(fs::exists(root.path / "Authored/Textures" / "a.ktx2"));
}

TEST_CASE("Derived files and directories cannot move independently", "[assetrename]")
{
	const DataRoot root("bernini_rename_derived");
	const Import   imported = WriteImport(root, "kirk", true);
	WriteSource(root.path / "Derived/SourceTextures/kirk/image.ktx2", { { 200, 0, 0, 255 } });
	const auto graph = root.Scan();
	for (const auto& key : { imported.mesh,
	                         imported.skeleton,
	                         imported.animations,
	                         std::string("Derived/SourceTextures/kirk/image.ktx2"),
	                         std::string("Derived/SourceTextures/kirk"),
	                         std::string("Derived/Meshes"),
	                         std::string("Derived") })
	{
		CAPTURE(key);
		CHECK_THROWS_WITH(
			planRename(graph, key, key + "-moved"),
			Catch::Matchers::ContainsSubstring("cannot move independently"));
	}
	WriteSource(root.path / "Authored/Textures/image.ktx2", { { 200, 0, 0, 255 } });
	CHECK_THROWS(planRename(
		root.Scan(),
		"Authored/Textures/image.ktx2",
		"Derived/SourceTextures/image.ktx2"));
}

TEST_CASE("Renaming a material re-points the import document that binds it", "[assetrename]")
{
	const DataRoot root("bernini_rename_importdoc");

	BMaterial material;
	material.name = "skin";
	core::file::write_atomic(
		root.path / "Authored/Materials" / "old.bmaterial",
		AssetCodec<BMaterial>::Serialize(material));

	ImportDocument document;
	document.bindings = { { "kirk[0]", "Authored/Materials/old.bmaterial" } };
	fs::create_directories(root.path / "Authored/Meshes");
	core::file::write_atomic(
		root.path / "Authored/Meshes" / "kirk.bimport",
		AssetCodec<ImportDocument>::Serialize(document));
	std::ofstream(root.path / "Authored/Meshes" / "kirk.glb") << "source";

	const RenamePlan plan = planRename(
		root.Scan(),
		"Authored/Materials/old.bmaterial",
		"Authored/Materials/new.bmaterial");
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	const ImportDocument rewritten =
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/kirk.bimport");
	REQUIRE(rewritten.bindings.size() == 1);
	CHECK(rewritten.bindings[0].material == "Authored/Materials/new.bmaterial");
}

TEST_CASE(
	"Renaming a material only an override names re-points the import document",
	"[assetrename][overrides]")
{
	const DataRoot root("bernini_rename_override");

	BMaterial material;
	material.name = "burnt";
	core::file::write_atomic(
		root.path / "Authored/Materials" / "old.bmaterial",
		AssetCodec<BMaterial>::Serialize(material));

	ImportDocument document;
	document.materialOverrides = { { "kirk[0]", "Burnt", "Authored/Materials/old.bmaterial" } };
	fs::create_directories(root.path / "Authored/Meshes");
	core::file::write_atomic(
		root.path / "Authored/Meshes" / "kirk.bimport",
		AssetCodec<ImportDocument>::Serialize(document));
	std::ofstream(root.path / "Authored/Meshes" / "kirk.glb") << "source";

	const RenamePlan plan = planRename(
		root.Scan(),
		"Authored/Materials/old.bmaterial",
		"Authored/Materials/new.bmaterial");
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	const ImportDocument rewritten =
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/kirk.bimport");
	REQUIRE(rewritten.materialOverrides.size() == 1);
	CHECK(
		rewritten.materialOverrides[0] ==
		MaterialOverrideBinding{ "kirk[0]", "Burnt", "Authored/Materials/new.bmaterial" });
}

TEST_CASE("Renaming a legacy imported source preserves every output", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_group");
	const Import   before = WriteImport(root, "kirk", true);
	const auto     mesh   = root.Source().GetFiles().Read(before.mesh);
	const auto     rig    = root.Source().GetFiles().Read(before.skeleton);
	const auto     clips  = root.Source().GetFiles().Read(before.animations);
	REQUIRE(
		Rename(root, before.source, "Authored/Meshes/hero.glb").status == RenameStatus::kRenamed);
	CHECK_FALSE(fs::exists(root.path / before.source));
	CHECK_FALSE(fs::exists(root.path / before.document));
	CHECK(root.Source().GetFiles().Read(before.mesh) == mesh);
	CHECK(root.Source().GetFiles().Read(before.skeleton) == rig);
	CHECK(root.Source().GetFiles().Read(before.animations) == clips);
	const auto document =
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/hero.bimport");
	CHECK(document.source == "Authored/Meshes/hero.glb");
	CHECK(document.skeleton == before.skeleton);
	CHECK(
		document.outputs ==
		std::vector<std::string>{ before.animations, before.mesh, before.skeleton });
}

TEST_CASE("A shared rig stays fixed when its source moves", "[assetrename]")
{
	// The one edge that makes a group rename more than five independent moves: a `.bskel` is
	// produced by one import and may be *bound* by another, whose document stores the path. Move
	// the rig without rewriting that document and the second model is skinned to nothing.
	const DataRoot root("bernini_rename_import_shared_rig");
	const Import   kirk = WriteImport(root, "kirk", /*rigged*/ true);

	auto bound     = ImportDocument();
	bound.skeleton = kirk.skeleton;
	bound.outputs  = { "Derived/Meshes/spock.bmesh" };
	SaveMesh(root, "spock.bmesh", {}, kirk.skeleton);
	core::file::write_atomic(
		root.path / "Authored/Meshes" / "spock.bimport",
		AssetCodec<ImportDocument>::Serialize(bound));
	std::ofstream(root.path / "Authored/Meshes" / "spock.glb") << "source";

	REQUIRE(Rename(root, kirk.source, "Authored/Meshes/hero.glb").status == RenameStatus::kRenamed);

	const ImportDocument after =
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/spock.bimport");
	CHECK(after.skeleton == kirk.skeleton);

	// The document is only half of what the second import says about the rig: its `.bmesh` stores
	// the same path as its own edge, and a mesh left naming the old file is skinned to nothing.
	CHECK(root.Source().LoadRegenMeshRefs("Derived/Meshes/spock.bmesh").skeleton == kirk.skeleton);

	// The second source's own outputs are none of this rename's business.
	CHECK(after.outputs == std::vector<std::string>{ "Derived/Meshes/spock.bmesh" });
	CHECK(fs::exists(root.path / "Derived/Meshes/spock.bmesh"));
}

TEST_CASE("An import with no rig preserves its output", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_norig");
	const Import   before = WriteImport(root, "prop", /*rigged*/ false);

	REQUIRE(
		Rename(root, before.source, "Authored/Meshes/crate.glb").status == RenameStatus::kRenamed);

	CHECK(fs::exists(root.path / "Authored/Meshes/crate.glb"));
	CHECK(fs::exists(root.path / "Authored/Meshes/crate.bimport"));
	CHECK(fs::exists(root.path / before.mesh));
	CHECK_FALSE(fs::exists(root.path / "Derived/Meshes/crate.bmesh"));

	const ImportDocument document =
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/crate.bimport");
	CHECK(document.outputs == std::vector<std::string>{ before.mesh });
}

TEST_CASE("An import document names the same move its source does", "[assetrename]")
{
	// A `.bimport` used to be refused outright, because renaming it alone orphaned the `.glb` whose
	// key is derived from its path. It now carries the group like the source does -- the two are one
	// asset under two names, so either spelling has to reach the same plan.
	const DataRoot root("bernini_rename_import_bydocument");
	const Import   before = WriteImport(root, "kirk", /*rigged*/ false);

	const RenamePlan plan =
		planRename(root.Scan(), before.document, "Authored/Meshes/hero.bimport");
	CHECK(plan.subject.from == before.document);
	CHECK(plan.assetType == AssetType::kImportDocument);

	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	CHECK(fs::exists(root.path / "Authored/Meshes/hero.glb"));
	CHECK(fs::exists(root.path / "Authored/Meshes/hero.bimport"));
	CHECK_FALSE(fs::exists(root.path / before.source));
}

// The document is pulled into the referrers by its own edges, and `outputs` is where most of those
// live -- so a document claiming none is the case that would silently keep a dead source. It is not
// hypothetical: it is a clips-only import, and every document written before `outputs` existed.
TEST_CASE("A document claiming no outputs still has its source rewritten", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_nooutputs");

	auto document   = ImportDocument();
	document.source = "Authored/Meshes/kirk.glb";
	fs::create_directories(root.path / "Authored/Meshes");
	std::ofstream(root.path / "Authored/Meshes/kirk.glb") << "source";
	core::file::write_atomic(
		root.path / "Authored/Meshes/kirk.bimport",
		AssetCodec<ImportDocument>::Serialize(document));

	REQUIRE(
		Rename(root, "Authored/Meshes/kirk.glb", "Authored/Meshes/hero.glb").status ==
		RenameStatus::kRenamed);

	CHECK(
		loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/hero.bimport").source ==
		"Authored/Meshes/hero.glb");
}

TEST_CASE("An imported output cannot be renamed separately", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_output");
	const Import   before = WriteImport(root, "kirk", false);
	CHECK_THROWS(planRename(root.Scan(), before.mesh, "Derived/Meshes/other.bmesh"));
	CHECK(fs::exists(root.path / before.mesh));
}

TEST_CASE("A missing source fails the rename, where a missing output does not", "[assetrename]")
{
	// The asymmetry the group rests on. A container is cache -- `Reimport` writes it back from the
	// source, so one already swept has nothing to move and the rename carries on. The `.glb` is
	// what `Reimport` reads, so nothing can put *it* back: a rename that proceeded without it would
	// report success and leave the one irreplaceable file under neither name.
	SECTION("a swept output is skipped")
	{
		const DataRoot root("bernini_rename_import_swept_output");
		const Import   before = WriteImport(root, "kirk", /*rigged*/ false);

		const RenamePlan plan = planRename(root.Scan(), before.source, "Authored/Meshes/hero.glb");
		fs::remove(root.path / before.mesh);

		REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);
		CHECK(fs::exists(root.path / "Authored/Meshes/hero.glb"));

		// The document names where the container will land, so the next reimport writes it there.
		const ImportDocument document =
			loadImportDocument(root.Source().GetFiles(), "Authored/Meshes/hero.bimport");
		CHECK(document.outputs == std::vector<std::string>{ before.mesh });
	}

	SECTION("a missing source fails, and the document stays put")
	{
		const DataRoot root("bernini_rename_import_lost_source");
		const Import   before = WriteImport(root, "kirk", /*rigged*/ false);

		const RenamePlan plan =
			planRename(root.Scan(), before.document, "Authored/Meshes/hero.bimport");
		fs::remove(root.path / before.source);

		CHECK(root.Source().RenameAsset(plan).status == RenameStatus::kFailed);
		CHECK(fs::exists(root.path / before.document));
		CHECK_FALSE(fs::exists(root.path / "Authored/Meshes/hero.bimport"));
	}

	SECTION("a document whose source was already gone cannot even be planned")
	{
		const DataRoot root("bernini_rename_import_plan_lost_source");
		const Import   before = WriteImport(root, "kirk", /*rigged*/ false);
		fs::remove(root.path / before.source);

		CHECK_THROWS(planRename(root.Scan(), before.document, "Authored/Meshes/hero.bimport"));
	}
}

TEST_CASE("A group destination taken by something else fails the plan", "[assetrename]")
{
	// The subject's destination is checked when the plan is made, so a caller can refuse before it
	// asks the user to confirm. What the group would land on is held to the same promise.
	const DataRoot root("bernini_rename_import_group_collision");
	const Import   before = WriteImport(root, "kirk", /*rigged*/ false);

	std::ofstream(root.path / "Authored/Meshes/hero.glb") << "occupied";

	CHECK_THROWS(planRename(root.Scan(), before.source, "Authored/Meshes/hero.glb"));
}

TEST_CASE("An imported source cannot be renamed into another kind of asset", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_kind");
	const Import   before = WriteImport(root, "kirk", /*rigged*/ false);

	// Without this the `.bmesh` would be swapped for a `.bimport` on the way in and the rename
	// would look ordinary.
	CHECK_THROWS(planRename(root.Scan(), before.source, "Authored/Meshes/hero.bmesh"));
	CHECK_THROWS(planRename(root.Scan(), "Authored/Meshes/ghost.glb", "Authored/Meshes/hero.glb"));
}

// Before the UI kinds were registered both paths threw "not an asset this project stores anything
// about": a rename could not even be planned for one.
TEST_CASE("A UI document renames as a leaf, and not into another kind", "[assetrename]")
{
	const DataRoot root("bernini_rename_ui");

	fs::create_directories(root.path / "Authored/UI");
	std::ofstream(root.path / "Authored/UI/menu.rml") << "<rml><body>menu</body></rml>";

	REQUIRE(
		Rename(root, "Authored/UI/menu.rml", "Authored/UI/main.rml").status ==
		RenameStatus::kRenamed);

	CHECK(fs::exists(root.path / "Authored/UI/main.rml"));
	CHECK_FALSE(fs::exists(root.path / "Authored/UI/menu.rml"));

	// A stylesheet is a different kind, so the extension cannot change under a rename any more
	// than a `.bmesh` could become a `.bmaterial`.
	CHECK_THROWS_AS(
		planRename(root.Scan(), "Authored/UI/main.rml", "Authored/UI/main.rcss"),
		std::runtime_error);
}

// `Reimport` finds a mesh source by walking `Authored/Meshes`, so a document moved out of it is one a
// fresh checkout can never produce its containers from.
TEST_CASE("An imported source cannot leave its category", "[assetrename]")
{
	const DataRoot root("bernini_rename_import_category");
	const Import   kirk = WriteImport(root, "kirk", /*rigged*/ false);

	CHECK_THROWS_WITH(
		planRename(root.Scan(), kirk.source, "Authored/Levels/kirk.glb"),
		Catch::Matchers::ContainsSubstring("Authored/Meshes"));
	CHECK_THROWS_WITH(
		planRename(root.Scan(), kirk.document, "Authored/EnvSources/kirk.bimport"),
		Catch::Matchers::ContainsSubstring("Authored/Meshes"));
	fs::create_directories(root.path / "Authored/Meshes/crew");
	CHECK_NOTHROW(planRename(root.Scan(), kirk.source, "Authored/Meshes/crew/kirk.glb"));
}
