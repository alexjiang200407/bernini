#include "Import/import_writers.h"
#include "Project/Project.h"

#include "util/QtSupport.h"

#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>

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
			for (const std::string_view category : Project::c_RequiredDirectories)
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
			return m_Root / Project::c_SkeletonsDirectoryName / "unit.bskel";
		}

		[[nodiscard]] fs::path
		Banim() const
		{
			return m_Root / Project::c_AnimationsDirectoryName / "unit.banim";
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

	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ false);

	REQUIRE(fs::exists(root.Bskel()));

	// Relative to the data root, like every other path a .bmesh holds -- an absolute one would name
	// this machine's temp directory and resolve nowhere else.
	CHECK(mesh.skeleton == "Skeletons/unit.bskel");

	const assetlib::Skeleton restored = assetlib::loadSkeleton(root.Bskel());
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

	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	REQUIRE(fs::exists(root.Banim()));

	const assetlib::AnimationSet clips = assetlib::loadAnimations(root.Banim());
	REQUIRE(clips.clips.size() == 1);
	CHECK(clips.stringPool.at(clips.clips[0].nameOffset) == "walk");

	// The clip set must name the rig by the same path the mesh does, or the two disagree about which
	// bone array their indices address.
	CHECK(clips.skeleton == mesh.skeleton);
	CHECK(assetlib::animationsMatchSkeleton(clips, assetlib::loadSkeleton(root.Bskel())));
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

	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	// Found through the containers as a load would find it: the box, the signature and the
	// skeleton all survive their round trip through disk.
	const std::optional<assetlib::Bounds> baked = assetlib::findPosedBounds(
		assetlib::loadAnimations(root.Banim()),
		mesh,
		0,
		assetlib::loadSkeleton(root.Bskel()));

	REQUIRE(baked.has_value());
	CHECK(baked->min.x == Catch::Approx(-1.0f));
	CHECK(baked->max.x == Catch::Approx(1.0f));
}

TEST_CASE("A static import writes no rig at all", "[importedrig]")
{
	const TempRoot  root;
	assetlib::BMesh mesh;

	editor::WriteImportedRig(
		assetlib::imp::BMeshImport(),
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	CHECK(mesh.skeleton.empty());
	CHECK_FALSE(fs::exists(root.Bskel()));
	CHECK_FALSE(fs::exists(root.Banim()));
}

// A failed or cancelled import may not leave a rig behind, and may not take one that was already
// there either -- the user was asked before it was overwritten, but only about the files it names.
TEST_CASE(
	"RollBackImport removes the rig an import wrote, and keeps what predated it",
	"[importedrig]")
{
	const TempRoot root;

	const fs::path kept = root.Data() / Project::c_SkeletonsDirectoryName / "existing.bskel";
	{
		std::ofstream out(kept, std::ios::binary);
		out << "not really a skeleton";
	}

	const auto      imported = SkinnedImport();
	assetlib::BMesh mesh;
	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	REQUIRE(fs::exists(root.Bskel()));
	REQUIRE(fs::exists(root.Banim()));

	const std::array<editor::ImportedFile, 3> files = { {
		{ root.Bskel(), false },
		{ root.Banim(), false },
		{ kept, true },
	} };

	editor::RollBackImport(files, {});

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
	const fs::path bmeshPath = root.Data() / "Meshes" / "unit.bmesh";

	assetlib::BMesh mesh;
	mesh.meshes.push_back(assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0 });

	assetlib::Submesh submesh{};
	submesh.indexType                     = assetlib::IndexType::kUint16;
	submesh.layout.attributeCount         = 1;
	submesh.layout.attributes[0].semantic = assetlib::VertexSemantic::kJoints0;
	submesh.layout.attributes[0].format   = assetlib::VertexFormat::kUint16x4;
	mesh.submeshes.push_back(submesh);

	REQUIRE(assetlib::isSkinned(mesh));
	REQUIRE_THROWS(assetlib::save(mesh, bmeshPath));

	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	REQUIRE_NOTHROW(assetlib::save(mesh, bmeshPath));
	CHECK(assetlib::load(bmeshPath).skeleton == "Skeletons/unit.bskel");
}

// The mechanism a clips-only import runs on: a second export of the same rig hashes to the same
// signature, so the clips can find the skeleton they belong to without the user tracking it.
TEST_CASE("A rig is found by signature, not by name", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh mesh;
	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ false);

	// The same rig, under a name nothing could guess from the animation file.
	const auto found = editor::FindMatchingSkeleton(root.Data(), imported.skeleton);
	CHECK(found == root.Bskel());

	// Directory order is unspecified, so silently picking one would make the .banim's reference
	// depend on the filesystem -- and scatter one rig's clips across two skeletons, which is exactly
	// what a VAT bake cannot fit a single bounding box around.
	SECTION("two rigs with the same signature are ambiguous, not a coin toss")
	{
		assetlib::BMesh second;
		const fs::path twin = root.Data() / Project::c_SkeletonsDirectoryName / "coyote_twin.bskel";
		editor::WriteImportedRig(
			SkinnedImport(),
			second,
			root.Data(),
			twin,
			root.Banim(),
			/*writeClips*/ false);

		REQUIRE(fs::exists(twin));
		CHECK_THROWS_AS(
			editor::FindMatchingSkeleton(root.Data(), imported.skeleton),
			std::runtime_error);
	}

	SECTION("a rig with a bone renamed is not a match")
	{
		assetlib::Skeleton other  = imported.skeleton;
		other.bones[1].nameOffset = other.stringPool.add("tail");

		CHECK(editor::FindMatchingSkeleton(root.Data(), other).empty());
	}

	// The signature covers names and parents and deliberately not the bind pose, which is what lets
	// a per-animation export whose rest pose drifted still attach: a clip replaces the pose whole.
	SECTION("a rig whose rest pose moved is still a match")
	{
		assetlib::Skeleton rebound            = imported.skeleton;
		rebound.bones[1].bindPose.translation = glm::vec3(0.0f, 99.0f, 0.0f);

		CHECK(editor::FindMatchingSkeleton(root.Data(), rebound) == root.Bskel());
	}
}

// The multi-file workflow end to end: the rig arrives with the first file, and a second file's clips
// attach to it without a second copy of the mesh or the skeleton.
TEST_CASE("Clips import on their own, attached to the rig already there", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh mesh = SkinnedQuad();
	editor::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ false);

	// On disk, where the clips import must find it: its box is measured against project meshes,
	// not against geometry it has no copy of.
	const fs::path meshPath = root.Data() / Project::c_MeshesDirectoryName / "unit.bmesh";
	fs::create_directories(meshPath.parent_path());
	assetlib::save(mesh, meshPath);

	const fs::path runPath = root.Data() / Project::c_AnimationsDirectoryName / "coyote_run.banim";
	editor::WriteImportedClips(imported, root.Data(), runPath);

	REQUIRE(fs::exists(runPath));

	const assetlib::AnimationSet clips = assetlib::loadAnimations(runPath);
	CHECK(clips.skeleton == mesh.skeleton);
	CHECK(assetlib::animationsMatchSkeleton(clips, assetlib::loadSkeleton(root.Bskel())));

	// A clips-only import serves the same loads a full one does, so it bakes the same boxes.
	CHECK(
		assetlib::findPosedBounds(clips, mesh, 0, assetlib::loadSkeleton(root.Bskel()))
			.has_value());

	// The point of the exercise: one rig, one mesh, many clip sets.
	CHECK_FALSE(fs::exists(root.Data() / Project::c_MeshesDirectoryName / "coyote_run.bmesh"));
}

TEST_CASE("Clips with no rig to attach to are refused", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	// Nothing has been imported yet, so there is no skeleton these clips could address. Writing them
	// anyway would leave a .banim naming a file that does not exist.
	CHECK_THROWS_AS(
		editor::WriteImportedClips(imported, root.Data(), root.Banim()),
		std::runtime_error);

	SECTION("and so is a file carrying no clips")
	{
		assetlib::BMesh mesh;
		editor::WriteImportedRig(
			imported,
			mesh,
			root.Data(),
			root.Bskel(),
			root.Banim(),
			/*writeClips*/ false);

		auto clipless = SkinnedImport();
		clipless.animations.clips.clear();

		CHECK_THROWS_AS(
			editor::WriteImportedClips(clipless, root.Data(), root.Banim()),
			std::runtime_error);
	}
}
