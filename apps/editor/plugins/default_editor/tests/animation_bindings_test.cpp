#include "Windows/AnimationEditor/animation_bindings.h"
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>  // IWYU pragma: keep
#include <assetlib/asset_refs.h>

#include "util/rig_containers.h"
#include <assetlib/project_layout.h>
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
	// One scan, so a case reads the way the panel does: the graph is asked for, then queried.
	assetlib::AssetRefGraph
	Graph(const std::filesystem::path& dataRoot)
	{
		return assetlib::AssetRefGraph::Scan(assetlib::AssetStore(dataRoot));
	}

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

	using editor::test::WriteBanim;
	using editor::test::WriteMesh;
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
		editor::ResolveAnimationBindings(Graph(root.Data()), "Derived/Skeletons/rig.bskel");

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
		editor::ResolveAnimationBindings(Graph(root.Data()), "Derived/Skeletons/rig.bskel");

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
		editor::ResolveAnimationBindings(Graph(root.Data()), "Derived/Skeletons/rig.bskel");

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
		editor::ResolveAnimationBindings(Graph(root.Data()), "Derived/Skeletons/rig.bskel"),
		std::runtime_error);
}

TEST_CASE("A static mesh resolves to nothing", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/rock.bmesh", "");
	WriteBanim(root.Data(), "Derived/Animations/walk.banim", "Derived/Skeletons/rig.bskel");

	const auto bindings = editor::ResolveAnimationBindings(Graph(root.Data()), "");

	CHECK(bindings.skeleton.empty());
	CHECK(bindings.animations.empty());
}

TEST_CASE("A project with no Animations directory has no candidates, not an error", "[animation]")
{
	const TempRoot root;
	WriteMesh(root.Data(), "Derived/Meshes/unit.bmesh", "Derived/Skeletons/rig.bskel");
	fs::remove_all(root.Data() / assetlib::c_AnimationsDirectoryName);

	const auto bindings =
		editor::ResolveAnimationBindings(Graph(root.Data()), "Derived/Skeletons/rig.bskel");

	CHECK(bindings.skeleton == "Derived/Skeletons/rig.bskel");
	CHECK(bindings.animations.empty());
}
