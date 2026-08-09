#include "Project/Project.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"

#include "util/QtSupport.h"

#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Skeleton.h>

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

	ContentExplorerWindow::WriteImportedRig(
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

	ContentExplorerWindow::WriteImportedRig(
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

TEST_CASE("A static import writes no rig at all", "[importedrig]")
{
	const TempRoot  root;
	assetlib::BMesh mesh;

	ContentExplorerWindow::WriteImportedRig(
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
TEST_CASE("RollBack removes the rig an import wrote, and keeps what predated it", "[importedrig]")
{
	const TempRoot root;

	const fs::path kept = root.Data() / Project::c_SkeletonsDirectoryName / "existing.bskel";
	{
		std::ofstream out(kept, std::ios::binary);
		out << "not really a skeleton";
	}

	const auto      imported = SkinnedImport();
	assetlib::BMesh mesh;
	ContentExplorerWindow::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ true);

	REQUIRE(fs::exists(root.Bskel()));
	REQUIRE(fs::exists(root.Banim()));

	const std::array<ContentExplorerWindow::ImportedFile, 3> files = { {
		{ root.Bskel(), false },
		{ root.Banim(), false },
		{ kept, true },
	} };

	ContentExplorerWindow::RollBack(files, {});

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

	ContentExplorerWindow::WriteImportedRig(
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
	ContentExplorerWindow::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ false);

	// The same rig, under a name nothing could guess from the animation file.
	const auto found = ContentExplorerWindow::FindMatchingSkeleton(root.Data(), imported.skeleton);
	CHECK(found == root.Bskel());

	SECTION("a rig with a bone renamed is not a match")
	{
		assetlib::Skeleton other  = imported.skeleton;
		other.bones[1].nameOffset = other.stringPool.add("tail");

		CHECK(ContentExplorerWindow::FindMatchingSkeleton(root.Data(), other).empty());
	}

	// The signature covers names and parents and deliberately not the bind pose, which is what lets
	// a per-animation export whose rest pose drifted still attach: a clip replaces the pose whole.
	SECTION("a rig whose rest pose moved is still a match")
	{
		assetlib::Skeleton rebound            = imported.skeleton;
		rebound.bones[1].bindPose.translation = glm::vec3(0.0f, 99.0f, 0.0f);

		CHECK(ContentExplorerWindow::FindMatchingSkeleton(root.Data(), rebound) == root.Bskel());
	}
}

// The multi-file workflow end to end: the rig arrives with the first file, and a second file's clips
// attach to it without a second copy of the mesh or the skeleton.
TEST_CASE("Clips import on their own, attached to the rig already there", "[importedrig]")
{
	const TempRoot root;
	const auto     imported = SkinnedImport();

	assetlib::BMesh mesh;
	ContentExplorerWindow::WriteImportedRig(
		imported,
		mesh,
		root.Data(),
		root.Bskel(),
		root.Banim(),
		/*writeClips*/ false);

	const fs::path runPath = root.Data() / Project::c_AnimationsDirectoryName / "coyote_run.banim";
	ContentExplorerWindow::WriteImportedClips(imported, root.Data(), runPath);

	REQUIRE(fs::exists(runPath));

	const assetlib::AnimationSet clips = assetlib::loadAnimations(runPath);
	CHECK(clips.skeleton == mesh.skeleton);
	CHECK(assetlib::animationsMatchSkeleton(clips, assetlib::loadSkeleton(root.Bskel())));

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
		ContentExplorerWindow::WriteImportedClips(imported, root.Data(), root.Banim()),
		std::runtime_error);

	SECTION("and so is a file carrying no clips")
	{
		assetlib::BMesh mesh;
		ContentExplorerWindow::WriteImportedRig(
			imported,
			mesh,
			root.Data(),
			root.Bskel(),
			root.Banim(),
			/*writeClips*/ false);

		auto clipless = SkinnedImport();
		clipless.animations.clips.clear();

		CHECK_THROWS_AS(
			ContentExplorerWindow::WriteImportedClips(clipless, root.Data(), root.Banim()),
			std::runtime_error);
	}
}
