#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/avatar.h>
#include <assetlib/codecs.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>

#include "MountAt.h"
#include "RefsSandbox.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// The `.bavatar`: the document itself, the convention that finds it, and the two operations that
// convention has to survive -- a reference scan and a rename.

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	// hip -> knee -> ankle -> toe, and one bone that is not on the leg at all, so a case can name a
	// bone the rig has and a bone it does not without inventing a second rig.
	constexpr std::array<const char*, 5> c_BoneNames = {
		{ "Dog Pelvis", "Dog L Thigh", "Dog L Calf", "Dog L Foot", "Dog L Toe" }
	};

	Skeleton
	MakeRig()
	{
		auto skeleton = Skeleton();
		for (uint32_t i = 0; i < c_BoneNames.size(); ++i)
		{
			auto bone = Bone();
			bone.bindPose =
				Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			bone.inverseBind = glm::mat4(1.0f);
			bone.parent      = i == 0 ? c_InvalidIndex : i - 1;
			bone.nameOffset  = skeleton.stringPool.add(c_BoneNames[i]);
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	Avatar
	MakeAvatar()
	{
		auto avatar = Avatar();
		avatar.legs.push_back({ "Dog L Thigh", "Dog L Calf", "Dog L Foot", "Dog L Toe" });
		return avatar;
	}

	std::string
	TextOf(const Avatar& avatar)
	{
		const std::vector<std::byte> bytes = AssetCodec<Avatar>::Serialize(avatar);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	Avatar
	Parse(std::string_view text)
	{
		return AssetCodec<Avatar>::Deserialize(
			std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()));
	}
}

TEST_CASE("An avatar round-trips through its codec", "[avatar]")
{
	const Avatar avatar = MakeAvatar();

	CHECK(Parse(TextOf(avatar)) == avatar);

	SECTION("the text is canonical, so two checkouts that agree on the content agree on the file")
	{
		// Re-serializing what a parse produced is the fixed point the format promises: sorted keys,
		// tab indent, one trailing newline. A document a person hand-edited into another spelling
		// of the same content saves back as this.
		const std::string once = TextOf(avatar);
		CHECK(TextOf(Parse(once)) == once);
		CHECK(once.ends_with("\n"));
		CHECK_FALSE(once.ends_with("\n\n"));
	}

	SECTION("an empty avatar is a document with a legs array, not an empty object")
	{
		// What the editor's *Create avatar* writes. `{}` would be indistinguishable from a file
		// nobody has opened, and the point of the action is that the document exists.
		const std::string text = TextOf(Avatar());
		CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"legs\""));
		CHECK(Parse(text).legs.empty());
	}

	SECTION("the clip weights round-trip, sorted, and are absent from a document naming none")
	{
		auto listed        = MakeAvatar();
		listed.clipWeights = { { "Attack", 0.5f }, { "Fall", 0.0f }, { "Jump_Up", 0.0f } };
		CHECK(Parse(TextOf(listed)) == listed);
		CHECK_THAT(TextOf(listed), Catch::Matchers::ContainsSubstring("\"plant\""));

		// An exception to a rule, so a document naming none does not carry the key.
		CHECK_THAT(TextOf(MakeAvatar()), !Catch::Matchers::ContainsSubstring("plant"));

		// At the float's shortest decimal, like every authored float: a hand-typed 0.6 comes back
		// as 0.6, not as the double the float happens to be.
		auto typed        = MakeAvatar();
		typed.clipWeights = { { "Attack", 0.6f } };
		CHECK_THAT(TextOf(typed), Catch::Matchers::ContainsSubstring("\"Attack\": 0.6\n"));
		CHECK_THAT(TextOf(typed), !Catch::Matchers::ContainsSubstring("0.60000"));
	}

	SECTION(
		"the unplanted list a document was written with reads as weight zero, and is not "
		"written back")
	{
		const Avatar read = Parse(R"({"legs":[],"unplanted":["Jump_Up","Fall"]})");
		CHECK(
			read.clipWeights ==
			std::vector<ClipPlantWeight>{ { "Fall", 0.0f }, { "Jump_Up", 0.0f } });

		const std::string text = TextOf(read);
		CHECK_THAT(text, !Catch::Matchers::ContainsSubstring("unplanted"));
		CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"plant\""));
		CHECK(Parse(text) == read);
	}

	SECTION("keys a reader does not know survive a save")
	{
		// The whole reason this is text: a newer branch authoring a bone mask must not have it
		// deleted by an older branch that opens the rig and saves.
		const Avatar      read = Parse(R"({"legs":[],"boneMask":["Dog Spine"],"note":"from Ada"})");
		const std::string text = TextOf(read);

		CHECK_THAT(text, Catch::Matchers::ContainsSubstring("boneMask"));
		CHECK_THAT(text, Catch::Matchers::ContainsSubstring("from Ada"));
	}
}

TEST_CASE("An avatar refuses a document it could not act on", "[avatar]")
{
	CHECK_THROWS(Parse("not json at all"));
	CHECK_THROWS(Parse(R"({"legs":"Dog L Foot"})"));
	CHECK_THROWS(Parse(R"({"legs":["Dog L Foot"]})"));

	// A leg missing a joint is refused rather than defaulted: an empty name resolves to no bone,
	// and a chain of three is not a chain the solve can walk.
	CHECK_THROWS(Parse(R"({"legs":[{"hip":"a","knee":"b","ankle":"c"}]})"));
	CHECK_THROWS(Parse(R"({"legs":[{"hip":"a","knee":"b","ankle":"c","toe":""}]})"));
	CHECK_THROWS(Parse(R"({"legs":[{"hip":"a","knee":"b","ankle":"c","toe":7}]})"));

	CHECK_THROWS(Parse(R"({"legs":[],"unplanted":"Jump_Up"})"));
	CHECK_THROWS(Parse(R"({"legs":[],"unplanted":[""]})"));
	CHECK_THROWS(Parse(R"({"legs":[],"unplanted":[3]})"));

	// A weight is a number in 0 to 1, and a clip is weighted once: twice would be two answers
	// to how far it plants, and a document from before the key could name a clip in both.
	CHECK_THROWS(Parse(R"({"legs":[],"plant":["Attack"]})"));
	CHECK_THROWS(Parse(R"({"legs":[],"plant":{"Attack":"half"}})"));
	CHECK_THROWS(Parse(R"({"legs":[],"plant":{"Attack":1.5}})"));
	CHECK_THROWS(Parse(R"({"legs":[],"plant":{"Attack":-0.1}})"));
	CHECK_THROWS(Parse(R"({"legs":[],"plant":{"":0.5}})"));
	CHECK_THROWS(Parse(R"({"legs":[],"plant":{"Fall":0.5},"unplanted":["Fall"]})"));
}

TEST_CASE("An avatar's names resolve to bone indices", "[avatar]")
{
	const Skeleton skeleton = MakeRig();

	const std::vector<AvatarLegChain> chains = resolveAvatar(MakeAvatar(), skeleton).legs;
	REQUIRE(chains.size() == 1);
	CHECK(chains[0] == AvatarLegChain{ 1, 2, 3, 4 });

	SECTION("a name the rig does not carry is refused, and the message says which")
	{
		auto avatar                  = MakeAvatar();
		avatar.legs[0].ankleBoneName = "Dog R Foot";

		CHECK_THROWS_WITH(
			resolveAvatar(avatar, skeleton),
			Catch::Matchers::ContainsSubstring("'Dog R Foot'"));
	}

	SECTION("resolution is against the rig as it stands, so a reordered one resolves afresh")
	{
		// The reason the document holds names and the `.bskel` is not touched: a re-export that
		// reorders the bone table changes every index, and an avatar of indices would then name
		// other joints without anything saying so.
		auto reordered = Skeleton();
		for (const char* name : { "Dog L Toe", "Dog L Foot", "Dog L Calf", "Dog L Thigh" })
		{
			auto bone = Bone();
			bone.bindPose =
				Transform{ glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			bone.inverseBind = glm::mat4(1.0f);
			bone.parent      = reordered.bones.empty() ?
			                       c_InvalidIndex :
			                       static_cast<uint32_t>(reordered.bones.size() - 1);
			bone.nameOffset  = reordered.stringPool.add(name);
			reordered.bones.push_back(bone);
		}

		const std::vector<AvatarLegChain> now = resolveAvatar(MakeAvatar(), reordered).legs;
		REQUIRE(now.size() == 1);
		CHECK(now[0] == AvatarLegChain{ 3, 2, 1, 0 });
	}

	SECTION("an indirect chain resolves; whether the pose pass can walk it is bgl's refusal")
	{
		// Deliberately not judged here. Two doors refusing one rule is two that can disagree, and
		// the one that cannot walk the chain is the one that owns the message naming the bone --
		// see IScene::AddRig.
		auto avatar                  = MakeAvatar();
		avatar.legs[0].kneeBoneName  = "Dog L Foot";
		avatar.legs[0].ankleBoneName = "Dog L Toe";
		avatar.legs[0].toeBoneName   = "Dog L Toe";

		CHECK_NOTHROW(resolveAvatar(avatar, skeleton));
	}

	SECTION("the clip weights come through by name, unresolved")
	{
		// Clip names are the `.banim`'s, which the skeleton knows nothing about; the plant
		// matches them when it measures.
		auto avatar        = MakeAvatar();
		avatar.clipWeights = { { "Attack", 0.5f }, { "Fall", 0.0f } };

		CHECK(resolveAvatar(avatar, skeleton).clipWeights == avatar.clipWeights);
	}
}

TEST_CASE("An avatar is found from its skeleton's key, and only from it", "[avatar]")
{
	CHECK(avatarKeyFor("Derived/Skeletons/dog.bskel") == "Authored/Skeletons/dog.bavatar");
	CHECK(skeletonKeyForAvatar("Authored/Skeletons/dog.bavatar") == "Derived/Skeletons/dog.bskel");

	SECTION("the two are inverses, subdirectories included")
	{
		// The tail beneath the category is carried across, so a project that files its rigs into
		// folders keeps the pairing rather than flattening it.
		const std::string skeleton = "Derived/Skeletons/quadrupeds/dog.bskel";
		CHECK(skeletonKeyForAvatar(avatarKeyFor(skeleton)) == skeleton);
	}

	SECTION("a key of the wrong kind or in the wrong half is refused")
	{
		CHECK_THROWS(avatarKeyFor("Derived/Skeletons/dog.banim"));
		CHECK_THROWS(avatarKeyFor("Derived/Meshes/dog.bskel"));
		CHECK_THROWS(avatarKeyFor("Authored/Skeletons/dog.bavatar"));
		CHECK_THROWS(skeletonKeyForAvatar("Derived/Skeletons/dog.bskel"));
	}
}

TEST_CASE("describe(Avatar) resolves each authored name against the rig", "[avatar][describe]")
{
	const Skeleton skeleton = MakeRig();

	auto avatar = MakeAvatar();
	avatar.legs.push_back({ "Dog L Thigh", "Dog L Calf", "Dog R Foot", "Dog L Toe" });

	const std::string text = StoreAt(fs::temp_directory_path()).Describe(avatar, &skeleton);

	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("legs         2"));
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("'Dog L Foot' [3]"));

	// Reported rather than thrown, unlike resolveAvatar: describe exists to show a person which
	// names went bad, so a rig re-exported with one joint renamed still prints the other three.
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("'Dog R Foot' NOT IN THE SKELETON"));

	avatar.clipWeights = { { "Attack", 0.5f } };
	CHECK_THAT(
		StoreAt(fs::temp_directory_path()).Describe(avatar, &skeleton),
		Catch::Matchers::ContainsSubstring("plant        'Attack' 0.50"));

	SECTION("with no rig to resolve against, the names print alone")
	{
		const std::string bare = StoreAt(fs::temp_directory_path()).Describe(avatar);
		CHECK_THAT(bare, Catch::Matchers::ContainsSubstring("'Dog L Foot'"));
		CHECK_THAT(bare, !Catch::Matchers::ContainsSubstring("NOT IN THE SKELETON"));
	}
}

TEST_CASE("The reference scan derives an avatar's skeleton from its key", "[avatar][assetrefs]")
{
	// Derived, not read: the edge exists whatever the document says, because what attaches an
	// avatar to a rig is the path it is written at.
	const DataRoot root("bernini_avatar_refs");
	fs::create_directories(root.path / c_SkeletonsDirectoryName);
	fs::create_directories(root.path / c_AvatarsDirectoryName);

	StoreAt(root.path).Save(MakeRig(), "Derived/Skeletons/dog.bskel");
	StoreAt(root.path).Save(MakeAvatar(), "Authored/Skeletons/dog.bavatar");

	const AssetRefGraph graph = root.Scan();
	CHECK(graph.avatarsScanned == 1);

	CHECK(
		ReferrerPaths(graph, "Derived/Skeletons/dog.bskel") ==
		std::vector<std::string>{ "Authored/Skeletons/dog.bavatar" });

	const std::vector<AssetRef> named = graph.ReferencesOf("Authored/Skeletons/dog.bavatar");
	REQUIRE(named.size() == 1);
	CHECK(named[0].target == "Derived/Skeletons/dog.bskel");
	CHECK(named[0].kind == RefKind::kAvatarSkeleton);
}

TEST_CASE("An avatar moves with the skeleton it belongs to", "[avatar][assetrename]")
{
	const DataRoot root("bernini_avatar_rename");
	fs::create_directories(root.path / c_SkeletonsDirectoryName);
	fs::create_directories(root.path / c_AvatarsDirectoryName);

	StoreAt(root.path).Save(MakeRig(), "Derived/Skeletons/dog.bskel");
	StoreAt(root.path).Save(MakeAvatar(), "Authored/Skeletons/dog.bavatar");

	SECTION("renaming the skeleton carries the avatar to the key that still finds it")
	{
		const RenamePlan plan = planRename(
			root.Scan(),
			"Derived/Skeletons/dog.bskel",
			"Derived/Skeletons/coyote.bskel");

		REQUIRE(plan.avatars.size() == 1);
		CHECK(plan.avatars[0].from == "Authored/Skeletons/dog.bavatar");
		CHECK(plan.avatars[0].to == "Authored/Skeletons/coyote.bavatar");

		CHECK(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);
		CHECK(fs::exists(root.path / "Authored/Skeletons/coyote.bavatar"));
		CHECK_FALSE(fs::exists(root.path / "Authored/Skeletons/dog.bavatar"));
	}

	SECTION("a skeleton with no avatar plans none")
	{
		fs::remove(root.path / "Authored/Skeletons/dog.bavatar");

		const RenamePlan plan = planRename(
			root.Scan(),
			"Derived/Skeletons/dog.bskel",
			"Derived/Skeletons/coyote.bskel");

		CHECK(plan.avatars.empty());
		CHECK(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);
	}

	SECTION("a directory of skeletons carries its avatars into the mirrored directory")
	{
		// The pair straddles the two halves, so the directory move itself can only ever carry one
		// end of it. A rename that reported success and left the avatars behind would detach every
		// rig in the folder at once.
		StoreAt(root.path).Save(MakeRig(), "Derived/Skeletons/quadrupeds/wolf.bskel");
		StoreAt(root.path).Save(MakeAvatar(), "Authored/Skeletons/quadrupeds/wolf.bavatar");

		const RenamePlan plan =
			planRename(root.Scan(), "Derived/Skeletons/quadrupeds", "Derived/Skeletons/mammals");

		REQUIRE(plan.IsDirectory());
		REQUIRE(plan.avatars.size() == 1);
		CHECK(plan.avatars[0].to == "Authored/Skeletons/mammals/wolf.bavatar");

		CHECK(root.Source().RenameAsset(plan).status == RenameStatus::kRenamed);
		CHECK(fs::exists(root.path / "Derived/Skeletons/mammals/wolf.bskel"));
		CHECK(fs::exists(root.path / "Authored/Skeletons/mammals/wolf.bavatar"));
		CHECK_FALSE(fs::exists(root.path / "Authored/Skeletons/quadrupeds/wolf.bavatar"));
	}

	SECTION("a directory of avatars cannot move, exactly as one avatar cannot")
	{
		StoreAt(root.path).Save(MakeRig(), "Derived/Skeletons/quadrupeds/wolf.bskel");
		StoreAt(root.path).Save(MakeAvatar(), "Authored/Skeletons/quadrupeds/wolf.bavatar");

		CHECK_THROWS_WITH(
			planRename(root.Scan(), "Authored/Skeletons/quadrupeds", "Authored/Skeletons/mammals"),
			Catch::Matchers::ContainsSubstring("Derived/Skeletons/quadrupeds/wolf.bskel"));
	}

	SECTION("renaming the avatar alone is refused, naming the skeleton to rename instead")
	{
		// It would not be stranded, it would be *detached*: the old key resolves to no avatar and
		// the new one to no skeleton, and no re-cook puts either right.
		CHECK_THROWS_WITH(
			planRename(
				root.Scan(),
				"Authored/Skeletons/dog.bavatar",
				"Authored/Skeletons/coyote.bavatar"),
			Catch::Matchers::ContainsSubstring("Derived/Skeletons/dog.bskel"));
	}
}
