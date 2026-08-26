#include <assetlib/Project.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <assetlib/pak.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

#include "asset_describe.h"
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>

#include "MountAt.h"
#include <catch2/catch_approx.hpp>

namespace
{
	namespace fs = std::filesystem;

	/** A data root that lasts as long as the test, under the OS temp directory. */
	class TempRoot
	{
	public:
		TempRoot()
		{
			m_Root = fs::temp_directory_path() /
			         ("bernini_rig_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			for (const std::string_view category : assetlib::c_RequiredDirectories)
				fs::create_directories(m_Root / category);
		}

		~TempRoot()
		{
			std::error_code ec;
			fs::remove_all(m_Root, ec);
		}

		TempRoot(const TempRoot&) = delete;
		TempRoot&
		operator=(const TempRoot&) = delete;

		[[nodiscard]] const fs::path&
		Data() const
		{
			return m_Root;
		}

		/** Where the import puts a rig: its own category directory, not beside the mesh. */
		[[nodiscard]] fs::path
		Bskel() const
		{
			return m_Root / assetlib::c_SkeletonsDirectoryName / "unit.bskel";
		}

		[[nodiscard]] fs::path
		Banim() const
		{
			return m_Root / assetlib::c_AnimationsDirectoryName / "unit.banim";
		}

		/** The same two files as keys, which is how the import writers address them. */
		[[nodiscard]] assetlib::AssetStore
		Store() const
		{
			return assetlib::AssetStore(m_Root);
		}

		[[nodiscard]] static std::string_view
		BskelKey()
		{
			return "Derived/Skeletons/unit.bskel";
		}

		[[nodiscard]] static std::string_view
		BanimKey()
		{
			return "Derived/Animations/unit.banim";
		}

	private:
		fs::path m_Root;
	};

	/** An import carrying a two-bone rig and one clip, as a skinned glTF would arrive. */
	assetlib::imp::BMeshImport
	SkinnedImport()
	{
		using namespace assetlib;

		imp::BMeshImport imported;

		Bone hips{};
		hips.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		hips.inverseBind = glm::mat4(1.0f);
		hips.parent      = c_InvalidIndex;
		hips.nameOffset  = imported.skeleton.stringPool.add("hips");
		imported.skeleton.bones.push_back(hips);

		Bone spine{};
		spine.bindPose    = { glm::vec3(0.0f, 1.0f, 0.0f),
			                  glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                  glm::vec3(1.0f) };
		spine.inverseBind = glm::mat4(1.0f);
		spine.parent      = 0;
		spine.nameOffset  = imported.skeleton.stringPool.add("spine");
		imported.skeleton.bones.push_back(spine);

		imported.animations.boneCount         = 2;
		imported.animations.skeletonSignature = skeletonSignature(imported.skeleton);

		AnimationClip walk{};
		walk.nameOffset  = imported.animations.stringPool.add("walk");
		walk.firstSample = 0;
		walk.frameCount  = 2;
		walk.duration    = 0.5f;
		walk.sampleRate  = 30.0f;
		imported.animations.clips.push_back(walk);

		const Transform rest{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		imported.animations.samples.assign(4, rest);

		return imported;
	}
}

// The rig is what makes a skinned import writable at all: assetlib::save refuses a mesh that carries
// joint indices while naming no skeleton, so before this the editor could not import a rigged glTF.
TEST_CASE("A skinned import writes its skeleton and the mesh names it", "[importedrig]")
{
	const TempRoot  root;
	const auto      imported = SkinnedImport();
	assetlib::BMesh mesh;

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ false,
		assetlib::SourceRef{});

	REQUIRE(fs::exists(root.Bskel()));

	// Relative to the data root, like every other path a .bmesh holds -- an absolute one would name
	// this machine's temp directory and resolve nowhere else.
	CHECK(mesh.skeleton == "Derived/Skeletons/unit.bskel");

	const assetlib::Skeleton restored = LoadAt<assetlib::Skeleton>(root.Bskel());
	REQUIRE(restored.bones.size() == 2);
	CHECK(restored.stringPool.at(restored.bones[1].nameOffset) == "spine");
	CHECK(restored.bones[1].parent == 0);

	CHECK_FALSE(fs::exists(root.Banim()));
}

TEST_CASE("The clips are written only when the import asked for them", "[importedrig]")
{
	const TempRoot  root;
	const auto      imported = SkinnedImport();
	assetlib::BMesh mesh;

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		assetlib::SourceRef{});

	REQUIRE(fs::exists(root.Banim()));

	const assetlib::AnimationSet clips = LoadAt<assetlib::AnimationSet>(root.Banim());
	REQUIRE(clips.clips.size() == 1);
	CHECK(clips.stringPool.at(clips.clips[0].nameOffset) == "walk");

	// The clip set must name the rig by the same path the mesh does, or the two disagree about which
	// bone array their indices address.
	CHECK(clips.skeleton == mesh.skeleton);
	CHECK(assetlib::animationsMatchSkeleton(clips, LoadAt<assetlib::Skeleton>(root.Bskel())));
}

namespace
{
	/** Two vertices welded to the hips: a mesh with a skin, so there is a box to measure. */
	assetlib::BMesh
	SkinnedQuad()
	{
		assetlib::BMesh mesh;

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 3;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              20 };
		submesh.layout.stride         = 28;

		for (const glm::vec3& position : { glm::vec3(-1.0f), glm::vec3(1.0f) })
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const std::array<uint16_t, 4> joints{};
			const std::array<uint16_t, 4> weights{ { 65535, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, joints.data(), sizeof(joints));
			std::memcpy(at + 20, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}

		mesh.submeshes.push_back(submesh);
		mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		return mesh;
	}
}

TEST_CASE("The import bakes the posed box beside the clips it writes", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	// The rig tests above pass an empty mesh on purpose -- no skin, no box.
	assetlib::BMesh mesh = SkinnedQuad();

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		assetlib::SourceRef{});

	// Found through the containers as a load would find it: the box, the signature and the
	// skeleton all survive their round trip through disk.
	const std::optional<assetlib::Bounds> baked = assetlib::findPosedBounds(
		LoadAt<assetlib::AnimationSet>(root.Banim()),
		mesh,
		LoadAt<assetlib::Skeleton>(root.Bskel()))[0];

	REQUIRE(baked.has_value());
	CHECK(baked->min.x == Catch::Approx(-1.0f));
	CHECK(baked->max.x == Catch::Approx(1.0f));
}

TEST_CASE("A static import writes no rig at all", "[importedrig]")
{
	const TempRoot  root;
	assetlib::BMesh mesh;

	// No rig, which is what a static mesh imports as.
	root.Store().WriteImportedRig(
		assetlib::Skeleton{},
		assetlib::AnimationSet{},
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		assetlib::SourceRef{});

	CHECK(mesh.skeleton.empty());
	CHECK_FALSE(fs::exists(root.Bskel()));
	CHECK_FALSE(fs::exists(root.Banim()));
}

// A failed or cancelled import may not leave a rig behind, and may not take one that was already
// there either -- the user was asked before it was overwritten, but only about the files it names.
TEST_CASE(
	"rollBackImport removes the rig an import wrote, and keeps what predated it",
	"[importedrig]")
{
	const TempRoot root;

	const fs::path kept = root.Data() / assetlib::c_SkeletonsDirectoryName / "existing.bskel";
	{
		std::ofstream out(kept, std::ios::binary);
		out << "not really a skeleton";
	}

	const auto      imported = SkinnedImport();
	assetlib::BMesh mesh;
	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		assetlib::SourceRef{});

	REQUIRE(fs::exists(root.Bskel()));
	REQUIRE(fs::exists(root.Banim()));

	const std::array<assetlib::ImportedFile, 3> files = { {
		{ root.Bskel(), false },
		{ root.Banim(), false },
		{ kept, true },
	} };

	assetlib::rollBackImport(files, {});

	CHECK_FALSE(fs::exists(root.Bskel()));
	CHECK_FALSE(fs::exists(root.Banim()));
	CHECK(fs::exists(kept));
}

// The rule the whole change exists to satisfy, asserted end to end rather than implied: a mesh
// carrying joint indices is one `save` refuses until something names its skeleton.
TEST_CASE("A skinned mesh is only writable once the rig names it", "[importedrig]")
{
	const TempRoot root;
	const auto     imported  = SkinnedImport();
	const fs::path bmeshPath = root.Data() / "Derived/Meshes" / "unit.bmesh";

	assetlib::BMesh mesh;
	mesh.meshes.push_back(assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	assetlib::Submesh submesh{};
	submesh.indexType                     = assetlib::IndexType::kUint16;
	submesh.layout.attributeCount         = 1;
	submesh.layout.attributes[0].semantic = assetlib::VertexSemantic::kJoints0;
	submesh.layout.attributes[0].format   = assetlib::VertexFormat::kUint16x4;
	mesh.submeshes.push_back(submesh);

	REQUIRE(assetlib::isSkinned(mesh));
	REQUIRE_THROWS(SaveAt(mesh, bmeshPath));

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		assetlib::SourceRef{});

	REQUIRE_NOTHROW(SaveAt(mesh, bmeshPath));
	CHECK(LoadAt<assetlib::BMesh>(bmeshPath).skeleton == "Derived/Skeletons/unit.bskel");
}

// The mechanism a clips-only import runs on: a second export of the same rig hashes to the same
// signature, so the clips can find the skeleton they belong to without the user tracking it.
TEST_CASE("A rig is found by signature, not by name", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh mesh;
	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ false,
		assetlib::SourceRef{});

	// The same rig, under a name nothing could guess from the animation file.
	const auto found = assetlib::AssetStore(root.Data()).FindMatchingSkeleton(imported.skeleton);
	CHECK(found == root.Bskel());

	// Directory order is unspecified, so silently picking one would make the .banim's reference
	// depend on the filesystem -- and scatter one rig's clips across two skeletons, which is exactly
	// what a VAT bake cannot fit a single bounding box around.
	SECTION("two rigs with the same signature are ambiguous, not a coin toss")
	{
		// Written directly, because no importer makes one any more: an import binds a rig it
		// matches rather than copying it. A project can still hold two from before that, or from
		// a hand-placed file, and this is what it is told.
		const fs::path twin =
			root.Data() / assetlib::c_SkeletonsDirectoryName / "coyote_twin.bskel";
		SaveAt(imported.skeleton, twin);

		REQUIRE(fs::exists(twin));
		CHECK_THROWS_AS(
			assetlib::AssetStore(root.Data()).FindMatchingSkeleton(imported.skeleton),
			std::runtime_error);

		// The message has to name both, as data-root-relative keys: an absolute path leaks the
		// machine's directory layout, and a bare file name does not say which of two folders to
		// look in -- the whole point being that the user has to pick one.
		try
		{
			static_cast<void>(
				assetlib::AssetStore(root.Data()).FindMatchingSkeleton(imported.skeleton));
			FAIL("FindMatchingSkeleton did not throw on two matching rigs");
		}
		catch (const std::runtime_error& e)
		{
			const std::string message = e.what();
			CHECK(message.find("Derived/Skeletons/unit.bskel") != std::string::npos);
			CHECK(message.find("Derived/Skeletons/coyote_twin.bskel") != std::string::npos);
			CHECK(message.find(root.Data().generic_string()) == std::string::npos);
		}
	}

	SECTION("a rig with a bone renamed is not a match")
	{
		assetlib::Skeleton other  = imported.skeleton;
		other.bones[1].nameOffset = other.stringPool.add("tail");

		CHECK(assetlib::AssetStore(root.Data()).FindMatchingSkeleton(other).empty());
	}

	// The signature covers names and parents and deliberately not the bind pose, which is what lets
	// a per-animation export whose rest pose drifted still attach: a clip replaces the pose whole.
	SECTION("a rig whose rest pose moved is still a match")
	{
		assetlib::Skeleton rebound            = imported.skeleton;
		rebound.bones[1].bindPose.translation = glm::vec3(0.0f, 99.0f, 0.0f);

		CHECK(assetlib::AssetStore(root.Data()).FindMatchingSkeleton(rebound) == root.Bskel());
	}
}

// The multi-file workflow end to end: the rig arrives with the first file, and a second file's clips
// attach to it without a second copy of the mesh or the skeleton.
TEST_CASE("Clips import on their own, attached to the rig already there", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh mesh = SkinnedQuad();
	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ false,
		assetlib::SourceRef{});

	// On disk, where the clips import must find it: its box is measured against project meshes,
	// not against geometry it has no copy of.
	const fs::path meshPath = root.Data() / assetlib::c_MeshesDirectoryName / "unit.bmesh";
	fs::create_directories(meshPath.parent_path());
	SaveAt(mesh, meshPath);

	const fs::path runPath = root.Data() / assetlib::c_AnimationsDirectoryName / "coyote_run.banim";
	root.Store().WriteImportedClips(
		imported.skeleton,
		imported.animations,
		"Derived/Animations/coyote_run.banim",
		assetlib::SourceRef{});

	REQUIRE(fs::exists(runPath));

	const assetlib::AnimationSet clips = LoadAt<assetlib::AnimationSet>(runPath);
	CHECK(clips.skeleton == mesh.skeleton);
	CHECK(assetlib::animationsMatchSkeleton(clips, LoadAt<assetlib::Skeleton>(root.Bskel())));

	// A clips-only import serves the same loads a full one does, so it bakes the same boxes.
	CHECK(
		assetlib::findPosedBounds(clips, mesh, LoadAt<assetlib::Skeleton>(root.Bskel()))[0]
			.has_value());

	// The point of the exercise: one rig, one mesh, many clip sets.
	CHECK_FALSE(fs::exists(root.Data() / assetlib::c_MeshesDirectoryName / "coyote_run.bmesh"));
}

// The shared-humanoid case, and the bug it used to be: two sources skinned to one rig. The second
// import used to fork a signature-matching duplicate, which then made every later clips-only import
// ambiguous -- so the workflow above could not be reached at all once a second mesh existed.
TEST_CASE("A second source skinned to a rig already here binds it", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh first = SkinnedQuad();
	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		first,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ false,
		assetlib::SourceRef{});
	REQUIRE(first.skeleton == TempRoot::BskelKey());

	// A second source, offered a `.bskel` key of its own -- which it must decline.
	assetlib::BMesh second = SkinnedQuad();
	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		second,
		"Derived/Skeletons/second.bskel",
		"Derived/Animations/second.banim",
		/*writeClips*/ false,
		assetlib::SourceRef{});

	CHECK(second.skeleton == TempRoot::BskelKey());
	CHECK_FALSE(fs::exists(root.Data() / "Derived/Skeletons/second.bskel"));
	CHECK(second.skeletonSignature == first.skeletonSignature);

	SECTION("so exactly one rig stands, and a clips-only import still resolves")
	{
		auto rigs = 0;
		for (const auto& entry :
		     fs::recursive_directory_iterator(root.Data() / assetlib::c_SkeletonsDirectoryName))
			rigs += entry.path().extension() == assetlib::c_SkeletonExtension ? 1 : 0;
		CHECK(rigs == 1);

		const fs::path meshPath = root.Data() / assetlib::c_MeshesDirectoryName / "second.bmesh";
		fs::create_directories(meshPath.parent_path());
		SaveAt(second, meshPath);

		// This is the throw the duplicate used to cause: two rigs matched, so which one the clips
		// belonged to depended on directory order.
		CHECK_NOTHROW(root.Store().WriteImportedClips(
			imported.skeleton,
			imported.animations,
			"Derived/Animations/walk.banim",
			assetlib::SourceRef{}));

		const assetlib::AnimationSet clips =
			LoadAt<assetlib::AnimationSet>(root.Data() / "Derived/Animations/walk.banim");
		CHECK(clips.skeleton == TempRoot::BskelKey());
	}
}

TEST_CASE("Clips with no rig to attach to are refused", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	// Nothing has been imported yet, so there is no skeleton these clips could address. Writing them
	// anyway would leave a .banim naming a file that does not exist.
	CHECK_THROWS_AS(
		root.Store().WriteImportedClips(
			imported.skeleton,
			imported.animations,
			TempRoot::BanimKey(),
			assetlib::SourceRef{}),
		std::runtime_error);

	SECTION("and so is a file carrying no clips")
	{
		assetlib::BMesh mesh;
		root.Store().WriteImportedRig(
			imported.skeleton,
			imported.animations,
			mesh,
			TempRoot::BskelKey(),
			TempRoot::BanimKey(),
			/*writeClips*/ false,
			assetlib::SourceRef{});

		auto clipless = SkinnedImport();
		clipless.animations.clips.clear();

		CHECK_THROWS_AS(
			root.Store().WriteImportedClips(
				clipless.skeleton,
				clipless.animations,
				TempRoot::BanimKey(),
				assetlib::SourceRef{}),
			std::runtime_error);
	}
}

/**
 * The gate for "one importer": a project, imported into by the same writers the CLI and the editor
 * both call, and then read back through the library the runtime reads with.
 *
 * apples.glb rather than suzanne.glb, which the plan named: suzanne carries no textures, so it
 * cannot pin the half of the file set that lands in Derived/SourceTextures/. Neither is skinned --
 * the rig
 * path is what every other case in this file covers.
 */
TEST_CASE("an import lands in the project's categories and reads back", "[importedmesh][project]")
{
	namespace fs = std::filesystem;

	const fs::path glb = "assets/apples.glb";
	REQUIRE(fs::exists(glb));

	const fs::path root = fs::temp_directory_path() / "bernini_import_roundtrip";
	fs::remove_all(root);

	assetlib::Project project = assetlib::Project::Create(root / "Round.bproj", "Round Trip");

	const fs::path dataRoot = project.GetDataDirectory();

	const auto imported = assetlib::loadFromGltf(glb);
	REQUIRE_FALSE(imported.textures.empty());
	REQUIRE_FALSE(imported.materials.empty());  // the glTF has them; the import must not carry them

	const fs::path textureDir = dataRoot / assetlib::c_SourceTexturesDirectoryName / "apples";

	const assetlib::AssetStore store(dataRoot);
	store.WriteTextures(imported, "Derived/SourceTextures/apples");

	assetlib::BMesh mesh = assetlib::toBMesh(imported);
	static_cast<void>(assetlib::generateTangents(mesh));
	assetlib::requireUniqueSubmeshNames(mesh);

	const assetlib::ImportTarget target{ "apples",
		                                 assetlib::c_DefaultSampleRate,
		                                 "Derived/SourceTextures/apples" };
	const assetlib::SourceRef    sourceRef = store.CopyImportedSource(glb, target);
	mesh.source                            = sourceRef;

	store.WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		"Derived/Skeletons/apples.bskel",
		"Derived/Animations/apples.banim",
		true,
		sourceRef);

	store.Save(mesh, "Derived/Meshes/apples.bmesh");
	store.WriteImportedDocument(target, &mesh);

	// The saved mesh carries the reference it will one day be regenerated by.
	CHECK(
		LoadAt<assetlib::BMesh>(dataRoot / assetlib::c_MeshesDirectoryName / "apples.bmesh")
			.source == sourceRef);

	SECTION("the source travels with the project, its document beside it")
	{
		CHECK(fs::exists(dataRoot / "Authored/Meshes/apples.glb"));

		const assetlib::ImportDocument document = assetlib::loadImportDocument(
			core::file::LooseFileSystem(dataRoot),
			"Authored/Meshes/apples.bimport");
		CHECK(document.sampleRate == assetlib::c_DefaultSampleRate);
		// No materials were attached, so the import records no bindings -- assetlib authors no
		// material documents (the editor is their sole author), so the document says so.
		CHECK(document.bindings.empty());
	}

	SECTION("the file set is the categories and nothing else")
	{
		auto written = std::vector<std::string>();
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dataRoot))
			if (entry.is_regular_file())
				written.push_back(fs::relative(entry.path(), dataRoot).generic_string());

		std::ranges::sort(written);

		auto expected = std::vector<std::string>{ "Derived/Meshes/apples.bmesh",
			                                      "Authored/Meshes/apples.bimport",
			                                      "Authored/Meshes/apples.glb" };
		for (const std::string& name : assetlib::importedTextureFileNames(imported))
			expected.push_back("Derived/SourceTextures/apples/" + name);
		std::ranges::sort(expected);

		// Exactly this: no Materials/, because the board that decides what a glTF material routes
		// where is the editor's and nothing in assetlib may guess at it. Not a rig either --
		// apples.glb carries no skin, and WriteImportedRig writes nothing for one that does not.
		CHECK(written == expected);
	}

	SECTION("the project reads back what was written")
	{
		project.ReloadStore();
		const assetlib::AssetStore& reloaded = project.GetStore();

		const assetlib::BMesh loaded =
			reloaded.Load<assetlib::BMesh>("Derived/Meshes/apples.bmesh");
		CHECK_FALSE(loaded.submeshes.empty());
		CHECK(loaded.materials.empty());
		for (const assetlib::Submesh& submesh : loaded.submeshes)
			CHECK(submesh.material == assetlib::c_InvalidIndex);

		// describe is what the CLI prints; it must not throw on an import with nothing attached.
		CHECK_FALSE(assetlib::describe(loaded, false).empty());

		// A reference scan finds the mesh and no dangling edge: an import that named a material it
		// never wrote would show up here, which is the failure this file set exists to rule out.
		const auto graph = assetlib::AssetRefGraph::Scan(store);
		CHECK(graph.meshesScanned == 1);
		CHECK(graph.broken.empty());

		// And it packs: Derived/SourceTextures is authoring source and stays out, so the mesh is
		// the payload.
		const assetlib::PackReport report =
			store.Pack(assetlib::PackDesc{ root / assetlib::c_DefaultArchiveName });
		CHECK(report.entries == 1);
	}

	fs::remove_all(root);
}

// The directory half of a rollback, which nothing covered: an import writes its textures into a
// folder of its own under Derived/SourceTextures/, and a failed one has to take that folder back
// down without
// ever taking the category down with it.
TEST_CASE("a rollback removes the folder an import made, never the category", "[importedrig]")
{
	namespace fs = std::filesystem;

	const TempRoot root;
	const fs::path category = root.Data() / assetlib::c_SourceTexturesDirectoryName;

	SECTION("a folder this import made goes")
	{
		const fs::path made = category / "coyote";
		fs::create_directories(made);
		std::ofstream(made / "tex0.ktx2") << "x";

		assetlib::rollBackImport({}, std::array{ assetlib::ImportedDir{ made, false, category } });

		CHECK_FALSE(fs::exists(made));
		CHECK(fs::is_directory(category));
	}

	SECTION("a folder that predated it stays, contents and all")
	{
		const fs::path existing = category / "shared";
		fs::create_directories(existing);
		std::ofstream(existing / "tex0.ktx2") << "x";

		assetlib::rollBackImport(
			{},
			std::array{ assetlib::ImportedDir{ existing, true, category } });

		CHECK(fs::exists(existing / "tex0.ktx2"));
	}

	// The guard compares whole paths, not file names. An import named after its own category makes a
	// folder whose *name* is the category's, and taking the category down instead would delete every
	// other import's textures with it.
	SECTION("an import named after its own category is still only its own folder")
	{
		const fs::path twin  = category / assetlib::c_SourceTexturesDirectoryName;
		const fs::path other = category / "unrelated";
		fs::create_directories(twin);
		fs::create_directories(other);
		std::ofstream(other / "tex0.ktx2") << "x";

		assetlib::rollBackImport({}, std::array{ assetlib::ImportedDir{ twin, false, category } });

		CHECK_FALSE(fs::exists(twin));
		CHECK(fs::exists(other / "tex0.ktx2"));
		CHECK(fs::is_directory(category));
	}

	SECTION("the category itself is never removable, however it is spelled")
	{
		assetlib::rollBackImport(
			{},
			std::array{ assetlib::ImportedDir{ category / "." / "", false, category } });

		CHECK(fs::is_directory(category));
	}
}

namespace
{
	/** One vertex welded to the root at `height` -- a rig authored against someone else's floor. */
	assetlib::BMesh
	MeshAt(float height)
	{
		using namespace assetlib;

		BMesh      mesh;
		const auto write = [&](const auto& value) {
			const auto* bytes = reinterpret_cast<const std::byte*>(&value);
			mesh.vertexData.insert(mesh.vertexData.end(), bytes, bytes + sizeof(value));
		};
		write(glm::vec3(0.0f, height, 0.0f));
		write(std::array<uint16_t, 4>{ { 0, 0, 0, 0 } });
		write(std::array<uint16_t, 4>{ { 65535, 0, 0, 0 } });

		Submesh submesh{};
		submesh.layout.attributeCount = 3;
		submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		submesh.layout.attributes[1]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 12 };
		submesh.layout.attributes[2]  = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 20 };
		submesh.layout.stride         = 28;
		submesh.vertexCount           = 1;
		mesh.submeshes.push_back(submesh);
		mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });
		return mesh;
	}

	/** An import document beside `name`'s copied source, authoring one clip's floor. */
	void
	WriteAuthoredGround(const TempRoot& root, std::string_view clip, float floor)
	{
		assetlib::ImportDocument document;
		document.clipFloors = { { std::string(clip), floor } };
		core::file::write_atomic(
			root.Data() / assetlib::c_MeshSourcesDirectoryName / "unit.bimport",
			assetlib::AssetCodec<assetlib::ImportDocument>::Serialize(document));
	}
}

// The whole feature, through the writer that ships it: the animal pack is authored anywhere from
// 0.57 below the floor to 0.92 above it, and nothing at runtime reconciles that -- so the cook does.
TEST_CASE("An import grounds the clips it writes", "[importedrig][grounding]")
{
	using namespace assetlib;

	const TempRoot root;
	auto           imported = SkinnedImport();
	BMesh          mesh     = MeshAt(2.0f);

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		SourceRef{});

	const AnimationSet stored = LoadAt<AnimationSet>(root.Banim());
	REQUIRE(stored.clips.size() == 1);
	CHECK(stored.clips[0].groundOffset == Catch::Approx(2.0f));

	// And the box baked beside them describes the grounded rig, not where it was authored: the same
	// box frames the editor's camera and culls the geom, so measuring it first would aim both at a
	// position nothing is ever drawn in.
	const Skeleton                           skeleton = LoadAt<Skeleton>(root.Bskel());
	const std::vector<std::optional<Bounds>> boxes    = findPosedBounds(stored, mesh, skeleton);
	REQUIRE(boxes[0].has_value());
	CHECK(boxes[0]->min.y == Catch::Approx(0.0f).margin(1e-5));
}

// The escape hatch has to reach the door every import goes through, not only the staleness-triggered
// reload: the whole point of authoring a floor is that the next cook uses it.
TEST_CASE("An import honours a floor its document authors", "[importedrig][grounding]")
{
	using namespace assetlib;

	const TempRoot root;
	auto           imported = SkinnedImport();
	BMesh          mesh     = MeshAt(2.0f);

	// The clip's own lowest point is 2.0; the author says it stands at 0.5.
	imported.animations.clips[0].nameOffset = imported.animations.stringPool.add("walk");
	WriteAuthoredGround(root, "walk", 0.5f);

	SourceRef source;
	source.key = std::format("{}/unit.glb", c_MeshSourcesDirectoryName);

	root.Store().WriteImportedRig(
		imported.skeleton,
		imported.animations,
		mesh,
		TempRoot::BskelKey(),
		TempRoot::BanimKey(),
		/*writeClips*/ true,
		source);

	const AnimationSet stored = LoadAt<AnimationSet>(root.Banim());
	REQUIRE(stored.clips.size() == 1);
	CHECK(stored.clips[0].groundOffset == Catch::Approx(0.5f));
}

// A .bimport is the one container a person edits by hand, so a re-import must not throw their edit
// away -- and if the cache key kept hashing it while the cook stopped applying it, the entry would
// read as fresh while standing in the wrong place.
TEST_CASE("Re-importing a source keeps the floors its document authors", "[importedrig][grounding]")
{
	using namespace assetlib;

	const TempRoot     root;
	const AssetStore   store = root.Store();
	const ImportTarget target{ "unit", c_DefaultSampleRate, {} };

	// The source the import copies in, standing outside the data root as a real one does.
	const fs::path source = root.Data() / "unit_source.glb";
	core::file::write_atomic(source, std::span<const std::byte>());
	WriteAuthoredGround(root, "walk", 0.5f);

	// The second import of the same source: it copies, re-keys and rewrites the document.
	const SourceRef ref = store.CopyImportedSource(source, target);
	store.WriteImportedDocument(target, nullptr);

	const ImportDocument rewritten =
		loadImportDocument(root.Data() / c_MeshSourcesDirectoryName / "unit.bimport");
	REQUIRE(rewritten.clipFloors.size() == 1);
	CHECK(rewritten.clipFloors[0] == ClipFloor{ "walk", 0.5f });

	// And the key that import recorded agrees with the file, so the entry it wrote does not read
	// stale the moment it is written -- which is what would happen if only one of the two carried
	// the authored floor.
	CHECK(ref.parametersHash == parametersHashOf(rewritten));
}
