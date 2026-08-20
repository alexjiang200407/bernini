#include "Windows/AnimationEditor/animation_bindings.h"
#include <assetlib/Project.h>

#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>

// Resolution is a query over the asset reference graph: the mesh names its rig, every .banim's
// kClipSkeleton edge names the rig it was authored against, and candidacy is those agreeing.
// Nothing here needs a device, a scene, or even a .bskel on disk.

namespace
{
	namespace fs = std::filesystem;

	class TempRoot
	{
	public:
		TempRoot()
		{
			m_Root = fs::temp_directory_path() /
			         ("bernini_bindings_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			for (const std::string_view category : assetlib::Project::c_RequiredDirectories)
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

	private:
		fs::path m_Root;
	};

	// A mesh that names its rig and nothing else -- a static attachment's shape, and all the
	// resolver reads.
	void
	WriteMesh(const fs::path& dataRoot, const fs::path& rel, std::string_view skeleton)
	{
		auto mesh     = assetlib::BMesh();
		mesh.skeleton = std::string(skeleton);
		fs::create_directories((dataRoot / rel).parent_path());
		assetlib::save(mesh, dataRoot / rel);
	}

	// A minimal valid clip set recording `skeleton` as its rig.
	void
	WriteBanim(const fs::path& dataRoot, const fs::path& rel, std::string_view skeleton)
	{
		auto animations      = assetlib::AnimationSet();
		animations.skeleton  = std::string(skeleton);
		animations.boneCount = 1;

		auto clip        = assetlib::AnimationClip();
		clip.nameOffset  = animations.stringPool.add("walk");
		clip.firstSample = 0;
		clip.frameCount  = 2;
		clip.sampleRate  = 30.0f;
		clip.duration    = 1.0f / 30.0f;
		animations.clips.push_back(clip);

		for (int frame = 0; frame < 2; ++frame)
			animations.samples.push_back(
				{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) });

		fs::create_directories((dataRoot / rel).parent_path());
		assetlib::saveAnimations(animations, dataRoot / rel);
	}
}

TEST_CASE("Bindings collect every .banim naming the mesh's rig, sorted", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/unit.bmesh", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/walk.banim", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/locomotion/run.banim", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/other.banim", "Skeletons/other.bskel");

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "Meshes/unit.bmesh");

	CHECK(bindings.skeleton == "Skeletons/rig.bskel");
	REQUIRE(bindings.animations.size() == 2);
	CHECK(bindings.animations[0] == "Animations/locomotion/run.banim");
	CHECK(bindings.animations[1] == "Animations/walk.banim");
}

TEST_CASE("A recorded path matches in normalized form, not by bytes", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/unit.bmesh", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/walk.banim", "./Skeletons//rig.bskel");

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "Meshes/unit.bmesh");

	REQUIRE(bindings.animations.size() == 1);
	CHECK(bindings.animations[0] == "Animations/walk.banim");
}

TEST_CASE(
	"A rigged mesh whose rig no clip file names has a skeleton and no candidates",
	"[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/unit.bmesh", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/other.banim", "Skeletons/other.bskel");

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "Meshes/unit.bmesh");

	CHECK(bindings.skeleton == "Skeletons/rig.bskel");
	CHECK(bindings.animations.empty());
}

TEST_CASE("An unreadable .banim fails resolution, as it fails the reference scan", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/unit.bmesh", "Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Animations/walk.banim", "Skeletons/rig.bskel");
	{
		std::ofstream out(root.Data() / "Animations/corrupt.banim", std::ios::binary);
		out << "not a clip set";
	}

	CHECK_THROWS_AS(
		editor::ResolveAnimationBindings(root.Data(), "Meshes/unit.bmesh"),
		std::runtime_error);
}

TEST_CASE("A static mesh resolves to nothing", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/rock.bmesh", "");
	WriteBanim(root.Data(), "Animations/walk.banim", "Skeletons/rig.bskel");

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "Meshes/rock.bmesh");

	CHECK(bindings.skeleton.empty());
	CHECK(bindings.animations.empty());
}

TEST_CASE("A project with no Animations directory has no candidates, not an error", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Meshes/unit.bmesh", "Skeletons/rig.bskel");
	fs::remove_all(root.Data() / assetlib::Project::c_AnimationsDirectoryName);

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "Meshes/unit.bmesh");

	CHECK(bindings.skeleton == "Skeletons/rig.bskel");
	CHECK(bindings.animations.empty());
}

TEST_CASE("A mesh that cannot be read throws", "[animation]")
{
	const TempRoot root;

	CHECK_THROWS_AS(
		editor::ResolveAnimationBindings(root.Data(), "Meshes/missing.bmesh"),
		std::runtime_error);
}
