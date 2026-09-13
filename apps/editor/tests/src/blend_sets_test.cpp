#include "Windows/AnimationEditor/blend_sets.h"
#include "util/rig_containers.h"

#include <QTemporaryDir>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <string>
#include <vector>

// Finding the blend sets a clip set has, and writing the first one. Both lifted clear of the panel,
// for the reason CreateEmptyAvatar is: the one thing the action cannot afford to get wrong is the
// path, and nothing that goes through a QMenu can be driven from a test.

namespace
{
	// One scan, so a case reads the way the panel does: the graph is asked for, then queried.
	assetlib::AssetRefGraph
	Graph(const std::filesystem::path& dataRoot)
	{
		return assetlib::AssetRefGraph::Scan(assetlib::AssetStore(dataRoot));
	}

	// A set on disk naming `animations`, which is the edge the reference scan reads.
	void
	WriteSet(
		const std::filesystem::path& root,
		const std::string&           key,
		const std::string&           animations)
	{
		auto set       = assetlib::BlendSet();
		set.name       = "authored by hand";
		set.animations = animations;
		assetlib::AssetStore(root).Save(set, key);
	}
}

TEST_CASE("A clip set's blend sets are the ones naming it", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());

	WriteSet(root, "Authored/Animations/loco.bblend", "Derived/Animations/loco.banim");
	WriteSet(root, "Authored/Animations/extra.bblend", "Derived/Animations/loco.banim");
	WriteSet(root, "Authored/Animations/other.bblend", "Derived/Animations/other.banim");

	SECTION("only the sets authored against that clip set come back, sorted")
	{
		const std::vector<std::string> sets =
			editor::ResolveBlendSets(Graph(root), "Derived/Animations/loco.banim");

		REQUIRE(sets.size() == 2);
		CHECK(sets[0] == "Authored/Animations/extra.bblend");
		CHECK(sets[1] == "Authored/Animations/loco.bblend");
	}

	SECTION("a clip set with none comes back empty rather than refusing")
	{
		CHECK(editor::ResolveBlendSets(Graph(root), "Derived/Animations/none.banim").empty());
	}

	SECTION("no clip set at all resolves to nothing without a scan")
	{
		CHECK(editor::ResolveBlendSets(Graph(root), "").empty());
	}
}

TEST_CASE("A blend set is shown on the meshes skinned to its clip set's rig", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());

	editor::test::WriteBanim(root, "Derived/Animations/loco.banim", "Derived/Skeletons/rig.bskel");
	WriteSet(root, "Authored/Animations/loco.bblend", "Derived/Animations/loco.banim");

	SECTION("every mesh on that rig comes back, sorted, and no other")
	{
		editor::test::WriteMesh(root, "Derived/Meshes/wolf.bmesh", "Derived/Skeletons/rig.bskel");
		editor::test::WriteMesh(root, "Derived/Meshes/dog.bmesh", "Derived/Skeletons/rig.bskel");
		editor::test::WriteMesh(root, "Derived/Meshes/cat.bmesh", "Derived/Skeletons/other.bskel");

		const std::vector<std::string> meshes =
			editor::ResolveBlendSetMeshes(Graph(root), "Authored/Animations/loco.bblend");

		REQUIRE(meshes.size() == 2);
		CHECK(meshes[0] == "Derived/Meshes/dog.bmesh");
		CHECK(meshes[1] == "Derived/Meshes/wolf.bmesh");
	}

	SECTION("a rig nothing is skinned to has nothing to show the set on")
	{
		editor::test::WriteMesh(root, "Derived/Meshes/cat.bmesh", "Derived/Skeletons/other.bskel");

		CHECK(
			editor::ResolveBlendSetMeshes(Graph(root), "Authored/Animations/loco.bblend").empty());
	}

	SECTION("a clip set recording no rig leads nowhere")
	{
		editor::test::WriteBanim(root, "Derived/Animations/loose.banim", "");
		WriteSet(root, "Authored/Animations/loose.bblend", "Derived/Animations/loose.banim");
		editor::test::WriteMesh(root, "Derived/Meshes/dog.bmesh", "Derived/Skeletons/rig.bskel");

		CHECK(
			editor::ResolveBlendSetMeshes(Graph(root), "Authored/Animations/loose.bblend").empty());
	}

	SECTION("a clip set that is not on disk leads nowhere")
	{
		WriteSet(root, "Authored/Animations/lost.bblend", "Derived/Animations/lost.banim");
		editor::test::WriteMesh(root, "Derived/Meshes/dog.bmesh", "Derived/Skeletons/rig.bskel");

		CHECK(
			editor::ResolveBlendSetMeshes(Graph(root), "Authored/Animations/lost.bblend").empty());
	}

	SECTION("a clip set is not a blend set, even one a mesh can play")
	{
		editor::test::WriteMesh(root, "Derived/Meshes/dog.bmesh", "Derived/Skeletons/rig.bskel");

		CHECK(editor::ResolveBlendSetMeshes(Graph(root), "Derived/Animations/loco.banim").empty());
	}
}

TEST_CASE("A new blend set is offered for each clip set with none at its key", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());

	editor::test::WriteBanim(root, "Derived/Animations/walk.banim", "Derived/Skeletons/rig.bskel");
	editor::test::WriteBanim(root, "Derived/Animations/run.banim", "Derived/Skeletons/rig.bskel");
	editor::test::WriteBanim(root, "Derived/Animations/idle.banim", "Derived/Skeletons/rig.bskel");

	SECTION("every clip set, sorted, while none has a set")
	{
		CHECK(
			editor::ClipSetsWithoutBlendSet(Graph(root)) ==
			std::vector<std::string>({ "Derived/Animations/idle.banim",
		                               "Derived/Animations/run.banim",
		                               "Derived/Animations/walk.banim" }));
	}

	SECTION("a clip set with a set at its key is not offered again")
	{
		WriteSet(root, "Authored/Animations/run.bblend", "Derived/Animations/run.banim");

		CHECK(
			editor::ClipSetsWithoutBlendSet(Graph(root)) ==
			std::vector<std::string>(
				{ "Derived/Animations/idle.banim", "Derived/Animations/walk.banim" }));
	}

	SECTION("a set stored elsewhere does not take the key a new one is written at")
	{
		WriteSet(root, "Authored/Sets/run_by_hand.bblend", "Derived/Animations/run.banim");

		CHECK(editor::ClipSetsWithoutBlendSet(Graph(root)).size() == 3);
	}
}

TEST_CASE("A project with no clip sets has nothing to start a blend set on", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());

	editor::test::WriteMesh(root, "Derived/Meshes/rock.bmesh", "");

	CHECK(editor::ClipSetsWithoutBlendSet(Graph(root)).empty());
}

TEST_CASE("The first blend set is written empty, beside its clip set", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());

	const std::string key = editor::CreateEmptyBlendSet(root, "Derived/Animations/loco.banim");
	CHECK(key == "Authored/Animations/loco.bblend");

	SECTION("it holds no spaces, and names the clip set it was authored against")
	{
		const auto set = assetlib::AssetStore(root).Load<assetlib::BlendSet>(key);
		CHECK(set.spaces.empty());
		CHECK(set.animations == "Derived/Animations/loco.banim");
	}

	SECTION("and is found by the scan straight away")
	{
		// The reason the empty document carries `animations` at all: nothing else attaches a set to
		// a clip set, so one written without it would be a file the panel could never offer again.
		const std::vector<std::string> sets =
			editor::ResolveBlendSets(Graph(root), "Derived/Animations/loco.banim");
		REQUIRE(sets.size() == 1);
		CHECK(sets[0] == key);
	}

	SECTION("a second create refuses rather than writing over the first")
	{
		CHECK_THROWS_WITH(
			editor::CreateEmptyBlendSet(root, "Derived/Animations/loco.banim"),
			Catch::Matchers::ContainsSubstring("already exists"));
	}

	SECTION("a clip set that is not one is refused by the convention")
	{
		CHECK_THROWS(editor::CreateEmptyBlendSet(root, "Derived/Meshes/rig.bmesh"));
		CHECK_THROWS(editor::CreateEmptyBlendSet(root, ""));
	}
}

TEST_CASE("An edited set is written back over what it was read from", "[animation][blend]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	const std::filesystem::path root = std::filesystem::path(dir.path().toStdWString());
	const std::string           key  = "Authored/Animations/loco.bblend";

	auto written       = assetlib::BlendSet();
	written.name       = "authored by hand";
	written.animations = "Derived/Animations/loco.banim";
	written.extraJson  = R"({"authoredBy":"a tool that came later"})";
	auto space         = assetlib::BlendSpace();
	space.name         = "locomotion";
	space.samples      = { { "walk", 1.5f }, { "run", 6.0f } };
	written.spaces.push_back(space);
	assetlib::AssetStore(root).Save(written, key);

	SECTION("what comes back is what an edit takes, and a save puts it all back")
	{
		assetlib::BlendSet set = editor::LoadBlendSet(root, key);
		REQUIRE(set.spaces.size() == 1);
		CHECK(set.spaces[0].samples.size() == 2);

		set.spaces[0].samples[1].parameter = 4.0f;
		editor::SaveBlendSet(root, key, set);

		const assetlib::BlendSet read = editor::LoadBlendSet(root, key);
		CHECK(read.spaces[0].samples[1].parameter == 4.0f);

		// The reason an edit loads the document rather than rebuilding it from what the acquire
		// resolved: neither the name nor a key written by something else survives that trip.
		CHECK(read.name == "authored by hand");
		CHECK_THAT(read.extraJson, Catch::Matchers::ContainsSubstring("a tool that came later"));
	}

	SECTION("a set the format refuses is not written at all")
	{
		assetlib::BlendSet set = editor::LoadBlendSet(root, key);
		set.spaces[0].samples.pop_back();  // one sample is a clip, not a space

		CHECK_THROWS(editor::SaveBlendSet(root, key, set));

		// Nothing was written, so what stands is still the set that was there.
		CHECK(editor::LoadBlendSet(root, key).spaces[0].samples.size() == 2);
	}
}
