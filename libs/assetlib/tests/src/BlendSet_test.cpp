#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <assetlib/codecs.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <core/file/file.h>

#include "MountAt.h"
#include "RefsSandbox.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// The `.bblend`: the document itself, what it refuses, and the two operations its stored reference
// to a `.banim` has to survive -- a reference scan and a rename of the clip set it names.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	constexpr std::string_view c_ClipsKey = "Derived/Animations/wolf.banim";

	BlendSet
	MakeSet()
	{
		auto set       = BlendSet();
		set.name       = "Wolf Locomotion";
		set.animations = std::string(c_ClipsKey);

		auto space = BlendSpace();
		space.name = "Locomotion";
		space.members.push_back({ "Idle", 0.0f });
		space.members.push_back({ "Walk", 1.5f });
		space.members.push_back({ "Run", 5.0f });
		set.spaces.push_back(std::move(space));

		return set;
	}

	/** The smallest clip set a `.bblend` can name: one bone, one clip. */
	AnimationSet
	MakeClips(const Skeleton& rig, std::string_view skeletonKey)
	{
		auto animations              = AnimationSet();
		animations.skeleton          = std::string(skeletonKey);
		animations.skeletonSignature = skeletonSignature(rig);
		animations.boneCount         = 1;

		auto clip       = AnimationClip();
		clip.nameOffset = animations.stringPool.add("Idle");
		clip.frameCount = 1;
		clip.sampleRate = c_DefaultSampleRate;
		animations.clips.push_back(clip);
		animations.samples.push_back(rig.bones[0].bindPose);

		return animations;
	}

	Skeleton
	MakeRig()
	{
		auto skeleton   = Skeleton();
		auto bone       = Bone();
		bone.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		bone.parent     = c_InvalidIndex;
		bone.nameOffset = skeleton.stringPool.add("root");
		skeleton.bones.push_back(bone);
		return skeleton;
	}

	std::string
	Text(const BlendSet& set)
	{
		const std::vector<std::byte> bytes = AssetCodec<BlendSet>::Serialize(set);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	BlendSet
	Parse(std::string_view text)
	{
		const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
		return AssetCodec<BlendSet>::Deserialize(bytes);
	}
}

TEST_CASE("A blend set round-trips through its document", "[blend][codec]")
{
	const BlendSet set = MakeSet();

	SECTION("field for field") { CHECK(Parse(Text(set)) == set); }

	SECTION("a key this build does not know survives the round trip")
	{
		// The whole point of the authored regime: a sibling branch's new field is not dropped by a
		// reader that has never heard of it.
		const std::string once = Text(Parse(R"({
			"animations": "Derived/Animations/wolf.banim",
			"name": "Wolf Locomotion",
			"note": "from Ada",
			"spaces": [{"members":[{"clip":"Idle","parameter":0.0},
			                       {"clip":"Run","parameter":5.0}],"name":"Locomotion"}]
		})"));

		CHECK_THAT(once, Catch::Matchers::ContainsSubstring(R"("note": "from Ada")"));

		// And is stable, not merely preserved once.
		CHECK(Text(Parse(once)) == once);
	}

	SECTION("a float keeps the shortest decimal a person typed")
	{
		CHECK_THAT(Text(set), Catch::Matchers::ContainsSubstring("1.5"));
	}
}

TEST_CASE("A blend set refuses what has no defined meaning", "[blend][codec]")
{
	SECTION("a space of fewer than two members")
	{
		// One member is a clip, and a clip is already a node under its own name.
		auto set = MakeSet();
		set.spaces[0].members.resize(1);
		CHECK_THROWS_WITH(
			validateBlendSet(set),
			Catch::Matchers::ContainsSubstring("at least two"));
	}

	SECTION("parameters that do not strictly increase")
	{
		auto set                           = MakeSet();
		set.spaces[0].members[2].parameter = set.spaces[0].members[1].parameter;
		CHECK_THROWS_WITH(
			validateBlendSet(set),
			Catch::Matchers::ContainsSubstring("strictly increase"));
	}

	SECTION("parameters out of order")
	{
		auto set                           = MakeSet();
		set.spaces[0].members[2].parameter = -1.0f;
		CHECK_THROWS(validateBlendSet(set));
	}

	SECTION("two spaces of one name")
	{
		auto set = MakeSet();
		set.spaces.push_back(set.spaces[0]);
		CHECK_THROWS_WITH(
			validateBlendSet(set),
			Catch::Matchers::ContainsSubstring("two spaces are named"));
	}

	SECTION("spaces naming clips of no clip set")
	{
		// Every name here gets an empty check, and the stored path is the one whose absence would
		// leave the spaces unresolvable and the rename with nothing to rewrite.
		auto set       = MakeSet();
		set.animations = {};
		CHECK_THROWS_WITH(validateBlendSet(set), Catch::Matchers::ContainsSubstring("no clip set"));
	}

	SECTION("but an empty document is what a create writes")
	{
		CHECK_NOTHROW(validateBlendSet(BlendSet()));
		CHECK(Parse(Text(BlendSet())) == BlendSet());
	}

	SECTION("a member naming no clip")
	{
		auto set                      = MakeSet();
		set.spaces[0].members[1].clip = {};
		CHECK_THROWS(validateBlendSet(set));
	}

	SECTION("a document whose space is malformed")
	{
		CHECK_THROWS(Parse(R"({"spaces":[{"name":"X","members":[{"clip":"Idle"}]}]})"));
	}

	SECTION("bytes that are not a text document")
	{
		const std::array<std::byte, 4> binary = {
			{ std::byte{ 'B' }, std::byte{ 'M' }, std::byte{ 'S' }, std::byte{ 'H' } }
		};
		CHECK_THROWS(AssetCodec<BlendSet>::Deserialize(binary));
	}
}

TEST_CASE("A blend set is authored, so the store refuses it under Derived", "[blend][codec]")
{
	const DataRoot root("bernini_blend_origin");
	fs::create_directories(root.path / "Authored/Animations");
	fs::create_directories(root.path / "Derived/Animations");

	CHECK_NOTHROW(StoreAt(root.path).Save(MakeSet(), "Authored/Animations/wolf.bblend"));
	CHECK_THROWS(StoreAt(root.path).Save(MakeSet(), "Derived/Animations/wolf.bblend"));
}

TEST_CASE("The reference scan reads the clip set a blend names", "[blend][assetrefs]")
{
	// Read out of the document, unlike an avatar's skeleton: a `.bblend` stores the path, so this
	// is an ordinary stored reference and a rename has something to rewrite.
	const DataRoot root("bernini_blend_refs");
	fs::create_directories(root.path / c_SkeletonsDirectoryName);
	fs::create_directories(root.path / "Derived/Animations");
	fs::create_directories(root.path / "Authored/Animations");

	const Skeleton rig = MakeRig();
	StoreAt(root.path).Save(rig, "Derived/Skeletons/wolf.bskel");
	StoreAt(root.path).Save(
		MakeClips(rig, "Derived/Skeletons/wolf.bskel"),
		std::string(c_ClipsKey));
	StoreAt(root.path).Save(MakeSet(), "Authored/Animations/wolf.bblend");

	const AssetRefGraph graph = root.Scan();
	CHECK(graph.blendSetsScanned == 1);

	const std::vector<AssetRef> named = graph.ReferencesOf("Authored/Animations/wolf.bblend");
	REQUIRE(named.size() == 1);
	CHECK(named[0].target == c_ClipsKey);
	CHECK(named[0].kind == RefKind::kBlendClips);
}

TEST_CASE("Renaming a clip set rewrites the blend that names it", "[blend][assetrename]")
{
	const DataRoot root("bernini_blend_rename");
	fs::create_directories(root.path / c_SkeletonsDirectoryName);
	fs::create_directories(root.path / "Derived/Animations");
	fs::create_directories(root.path / "Authored/Animations");

	const Skeleton rig = MakeRig();
	StoreAt(root.path).Save(rig, "Derived/Skeletons/wolf.bskel");
	StoreAt(root.path).Save(
		MakeClips(rig, "Derived/Skeletons/wolf.bskel"),
		std::string(c_ClipsKey));
	StoreAt(root.path).Save(MakeSet(), "Authored/Animations/wolf.bblend");

	const RenamePlan plan =
		planRename(root.Scan(), c_ClipsKey, "Derived/Animations/dire_wolf.banim");
	REQUIRE(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);

	const BlendSet after = StoreAt(root.path).Load<BlendSet>("Authored/Animations/wolf.bblend");
	CHECK(after.animations == "Derived/Animations/dire_wolf.banim");

	// The spaces are untouched: a rename moves a path, and a clip name is not one.
	CHECK(after.spaces == MakeSet().spaces);
}

TEST_CASE(
	"migrate re-saves a non-canonical blend set and leaves a canonical one",
	"[blend][migrate]")
{
	const DataRoot root("bernini_blend_migrate");
	fs::create_directories(root.path / "Authored/Animations");

	const std::vector<std::byte> canonical = AssetCodec<BlendSet>::Serialize(MakeSet());
	core::file::write_atomic(root.path / "Authored/Animations/canonical.bblend", canonical);

	// The same content spelled as a hand edit or a merge leaves it: unsorted keys, spaces.
	constexpr std::string_view c_Older =
		R"({"name": "Wolf Locomotion", "animations": "Derived/Animations/wolf.banim", )"
		R"("spaces": [{"members": [{"clip": "Idle", "parameter": 0.0}, )"
		R"({"clip": "Walk", "parameter": 1.5}, {"clip": "Run", "parameter": 5.0}], )"
		R"("name": "Locomotion"}]})";
	const auto older = std::as_bytes(std::span(c_Older.data(), c_Older.size()));
	core::file::write_atomic(
		root.path / "Authored/Animations/older.bblend",
		std::vector<std::byte>(older.begin(), older.end()));

	const auto report = AssetStore(root.path).Migrate(false);

	CHECK(report.Count(MigratedFile::Outcome::kUnchanged) == 1);
	CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);

	// Byte-identical, which is what makes migrate safe to run on a clean tree.
	CHECK(
		AssetCodec<BlendSet>::Serialize(StoreAt(root.path).Load<BlendSet>(
			"Authored/Animations/canonical.bblend")) == canonical);
	CHECK(StoreAt(root.path).Load<BlendSet>("Authored/Animations/older.bblend") == MakeSet());
}
