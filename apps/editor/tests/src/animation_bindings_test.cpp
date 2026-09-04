#include "Windows/AnimationEditor/animation_bindings.h"
#include <assetlib/Project.h>  // IWYU pragma: keep

#include "StoreAt.h"
#include <assetlib/project_layout.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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
		SaveAt(mesh, dataRoot / rel);
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
		SaveAt(animations, dataRoot / rel);
	}
}

TEST_CASE("Bindings collect every .banim naming the mesh's rig, sorted", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Derived/Animations/walk.banim", "Derived/Skeletons/rig.bskel");
	WriteBanim(
		root.Data(),
		"Derived/Animations/locomotion/run.banim",
		"Derived/Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Derived/Animations/other.banim", "Derived/Skeletons/other.bskel");

	const auto bindings =
		editor::ResolveAnimationBindings(root.Data(), "Derived/Skeletons/rig.bskel");

	CHECK(bindings.skeleton == "Derived/Skeletons/rig.bskel");
	REQUIRE(bindings.animations.size() == 2);
	CHECK(bindings.animations[0] == "Derived/Animations/locomotion/run.banim");
	CHECK(bindings.animations[1] == "Derived/Animations/walk.banim");
}

TEST_CASE("A recorded path matches in normalized form, not by bytes", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Derived/Animations/walk.banim", "./Derived/Skeletons//rig.bskel");

	const auto bindings =
		editor::ResolveAnimationBindings(root.Data(), "Derived/Skeletons/rig.bskel");

	REQUIRE(bindings.animations.size() == 1);
	CHECK(bindings.animations[0] == "Derived/Animations/walk.banim");
}

TEST_CASE(
	"A rigged mesh whose rig no clip file names has a skeleton and no candidates",
	"[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Derived/Animations/other.banim", "Derived/Skeletons/other.bskel");

	const auto bindings =
		editor::ResolveAnimationBindings(root.Data(), "Derived/Skeletons/rig.bskel");

	CHECK(bindings.skeleton == "Derived/Skeletons/rig.bskel");
	CHECK(bindings.animations.empty());
}

TEST_CASE("An unreadable .banim fails resolution, as it fails the reference scan", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	WriteBanim(root.Data(), "Derived/Animations/walk.banim", "Derived/Skeletons/rig.bskel");
	{
		std::ofstream out(root.Data() / "Derived/Animations/corrupt.banim", std::ios::binary);
		out << "not a clip set";
	}

	CHECK_THROWS_AS(
		editor::ResolveAnimationBindings(root.Data(), "Derived/Skeletons/rig.bskel"),
		std::runtime_error);
}

TEST_CASE("A static mesh resolves to nothing", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/rock.bmesh", "");
	WriteBanim(root.Data(), "Derived/Animations/walk.banim", "Derived/Skeletons/rig.bskel");

	const auto bindings = editor::ResolveAnimationBindings(root.Data(), "");

	CHECK(bindings.skeleton.empty());
	CHECK(bindings.animations.empty());
}

TEST_CASE("A project with no Animations directory has no candidates, not an error", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	fs::remove_all(root.Data() / assetlib::c_AnimationsDirectoryName);

	const auto bindings =
		editor::ResolveAnimationBindings(root.Data(), "Derived/Skeletons/rig.bskel");

	CHECK(bindings.skeleton == "Derived/Skeletons/rig.bskel");
	CHECK(bindings.animations.empty());
}
