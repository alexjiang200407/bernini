#include "MountAt.h"
#include "RefsSandbox.h"
#include "gltf_skin.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/avatar.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/LooseFileSystem.h>
#include <core/glm.h>
#include <core/platform/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tiny_gltf.h>
#include <utility>
#include <vector>

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	Avatar
	ParseParts(std::string_view text)
	{
		return AssetCodec<Avatar>::Deserialize(std::as_bytes(std::span(text)));
	}

	Skeleton
	PartRig()
	{
		auto rig = Skeleton();
		for (const auto& [name, parent] :
		     std::initializer_list<std::pair<const char*, uint32_t>>{ { "pelvis", c_InvalidIndex },
		                                                              { "arm", 0 },
		                                                              { "tail", 0 },
		                                                              { "elbow", 1 },
		                                                              { "hand", 3 },
		                                                              { "finger", 4 } })
		{
			auto bone        = Bone();
			bone.parent      = parent;
			bone.nameOffset  = rig.stringPool.add(name);
			bone.bindPose    = Transform{ glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1) };
			bone.inverseBind = glm::mat4(1);
			rig.bones.push_back(bone);
		}
		return rig;
	}

	std::vector<std::string>
	PartNames(const Skeleton& rig, const ResolvedAvatar& avatar, const std::string& part)
	{
		std::vector<std::string> names;
		for (const uint32_t bone : avatar.parts.at(part))
			names.emplace_back(rig.stringPool.at(rig.bones[bone].nameOffset));
		return names;
	}
}

TEST_CASE("Avatar parts preserve authored meaning and extension fields", "[avatar][parts]")
{
	const Avatar avatar = ParseParts(R"({"parts":{
		"pelvis":"pelvis",
		"left_arm":{"start":"arm","end":"hand","note":"reach"},
		"custom_feeler":{"start":"tail","end":"tail","future":true}
	},"author":"Ada"})");
	const auto   bytes  = AssetCodec<Avatar>::Serialize(avatar);
	CHECK(AssetCodec<Avatar>::Deserialize(bytes) == avatar);
	CHECK(AssetCodec<Avatar>::Serialize(AssetCodec<Avatar>::Deserialize(bytes)) == bytes);
	const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\n\t\"parts\""));
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"pelvis\": \"pelvis\""));
	CHECK(text.ends_with("\n"));
	const auto resolved = resolveAvatar(avatar, PartRig());
	CHECK(resolved.parts.at("custom_feeler") == std::vector<uint32_t>{ 2 });
	CHECK_FALSE(resolved.parts.contains("right_arm"));
	CHECK(resolveAvatar(ParseParts("{}"), PartRig()).parts.empty());
}

TEST_CASE("A part follows ancestry without including siblings or descendants", "[avatar][parts]")
{
	const auto avatar   = ParseParts(R"({"parts":{
		"pelvis":"pelvis","left_arm":{"start":"arm","end":"hand"},
		"forearm":{"start":"elbow","end":"hand"}
	}})");
	const auto resolved = resolveAvatar(avatar, PartRig());
	CHECK(resolved.parts.at("pelvis") == std::vector<uint32_t>{ 0 });
	CHECK(resolved.parts.at("left_arm") == std::vector<uint32_t>{ 1, 3, 4 });
	CHECK(resolved.parts.at("forearm") == std::vector<uint32_t>{ 3, 4 });
}

TEST_CASE("Invalid part authoring is rejected before it can lose meaning", "[avatar][parts]")
{
	for (const std::string_view text : { R"({"parts":[]})",
	                                     R"({"parts":{"":"pelvis"}})",
	                                     R"({"parts":{"arm":""}})",
	                                     R"({"parts":{"arm":7}})",
	                                     R"({"parts":{"arm":{"start":"arm"}}})",
	                                     R"({"parts":{"arm":{"start":"arm","end":null}}})",
	                                     R"({"parts":{"arm":{"start":"","end":"hand"}}})" })
	{
		CAPTURE(text);
		CHECK_THROWS_WITH(ParseParts(text), Catch::Matchers::ContainsSubstring("part"));
	}
	Avatar avatar;
	avatar.parts["arm"] = { "arm", "" };
	CHECK_THROWS_WITH(
		AssetCodec<Avatar>::Serialize(avatar),
		Catch::Matchers::ContainsSubstring("'arm'"));
	CHECK_THROWS_WITH(
		resolveAvatar(avatar, PartRig()),
		Catch::Matchers::ContainsSubstring("'arm'"));
}

TEST_CASE("A broken part diagnoses the role and the bone instead of guessing", "[avatar][parts]")
{
	auto avatar = ParseParts(R"({"parts":{"left_arm":{"start":"arm","end":"hand"}}})");
	auto rig    = PartRig();
	SECTION("missing endpoint")
	{
		avatar.parts.at("left_arm").endBoneName = "missing";
		CHECK_THROWS_WITH(
			resolveAvatar(avatar, rig),
			Catch::Matchers::ContainsSubstring("part 'left_arm' names bone 'missing'"));
	}
	SECTION("sibling endpoint")
	{
		avatar.parts.at("left_arm").endBoneName = "tail";
		CHECK_THROWS_WITH(
			resolveAvatar(avatar, rig),
			Catch::Matchers::ContainsSubstring(
				"part 'left_arm' end 'tail' is not descended from start 'arm'"));
	}
	SECTION("reversed endpoints")
	{
		avatar.parts.at("left_arm") = { "hand", "arm" };
		CHECK_THROWS_WITH(
			resolveAvatar(avatar, rig),
			Catch::Matchers::ContainsSubstring("not descended"));
	}
	SECTION("ambiguous endpoint")
	{
		rig.bones[2].nameOffset = rig.bones[1].nameOffset;
		CHECK_THROWS_WITH(
			resolveAvatar(avatar, rig),
			Catch::Matchers::ContainsSubstring("part 'left_arm' names ambiguous bone 'arm'"));
	}
	SECTION("a malformed skeleton cannot loop or read outside the bone table")
	{
		rig.bones[3].parent = 100;
		CHECK_THROWS(resolveAvatar(avatar, rig));
		rig.bones[3].parent = 4;
		CHECK_THROWS(resolveAvatar(avatar, rig));
	}
}

TEST_CASE("Named parts coexist with the existing foot-plant contract", "[avatar][parts]")
{
	const auto avatar   = ParseParts(R"({
		"legs":[{"hip":"arm","knee":"elbow","ankle":"hand","toe":"finger"}],
		"plant":{"Jump":0},"parts":{"pelvis":"pelvis","left_leg":{"start":"arm","end":"hand"}}
	})");
	const auto resolved = resolveAvatar(avatar, PartRig());
	CHECK(resolved.legs == std::vector<AvatarLegChain>{ { 1, 3, 4, 5 } });
	CHECK(resolved.clipWeights == std::vector<ClipPlantWeight>{ { "Jump", 0 } });
	const DataRoot root("bernini_avatar_parts");
	StoreAt(root.path).Save(avatar, "Authored/Skeletons/unit.bavatar");
	const auto files = core::file::LooseFileSystem(root.path);
	CHECK(avatarForRig(files, "Derived/Skeletons/unit.bskel", PartRig()) == resolved);
	CHECK(loadAvatar(files, "Authored/Skeletons/unit.bavatar") == avatar);
}

TEST_CASE(
	"Describe reports every part even when another part is invalid",
	"[avatar][parts][describe]")
{
	const auto        avatar = ParseParts(R"({"parts":{
		"bad":{"start":"arm","end":"tail"},"missing":"absent","pelvis":"pelvis"
	}})");
	const auto        rig    = PartRig();
	const auto        store  = StoreAt(std::filesystem::temp_directory_path());
	const std::string text   = store.Describe(avatar, &rig);
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("parts        3"));
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("not descended"));
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("NOT IN THE SKELETON"));
	CHECK_THAT(text, Catch::Matchers::ContainsSubstring("chain 'pelvis' [0]"));
	CHECK_THAT(store.Describe(avatar), !Catch::Matchers::ContainsSubstring("INVALID"));
}

TEST_CASE("Animal part examples survive an import that changes bone indices", "[avatar][parts]")
{
	const auto files = core::file::LooseFileSystem("assets/avatar_parts");
	for (const std::string species : { "Bear", "Owl", "Cat" })
	{
		CAPTURE(species);
		const auto    avatar = loadAvatar(files, species + ".bavatar");
		std::ifstream input(
			std::filesystem::path("assets/avatar_parts") / (species + ".skeleton.json"));
		REQUIRE(input.good());
		const auto bones = nlohmann::json::parse(input);
		auto       model = tinygltf::Model();
		model.nodes.resize(bones.size());
		model.skins.emplace_back();
		for (size_t i = 0; i < bones.size(); ++i)
		{
			model.nodes[i].name = bones[i][0].get<std::string>();
			const int parent    = bones[i][1].get<int>();
			if (parent >= 0)
				model.nodes[static_cast<size_t>(parent)].children.push_back(static_cast<int>(i));
			model.skins[0].joints.push_back(static_cast<int>(i));
		}
		const auto before = importSkin(model).skeleton;
		std::ranges::reverse(model.skins[0].joints);
		const auto after = importSkin(model).skeleton;
		CHECK(skeletonSignature(before) != skeletonSignature(after));
		const auto oldParts = resolveAvatar(avatar, before);
		const auto newParts = resolveAvatar(avatar, after);
		CHECK(oldParts.parts.size() == 10);
		for (const auto& [name, part] : avatar.parts)
		{
			CAPTURE(name);
			CHECK(PartNames(before, oldParts, name) == PartNames(after, newParts, name));
			CHECK(PartNames(after, newParts, name).front() == part.startBoneName);
			CHECK(PartNames(after, newParts, name).back() == part.endBoneName);
		}
		const std::string limb = species == "Owl" ? "left_wing" : "left_arm";
		CHECK(
			PartNames(after, newParts, limb) == std::vector<std::string>{ species + " L UpperArm",
		                                                                  species + " L Forearm",
		                                                                  species + " L Hand" });
		const size_t expectedTailBones = species == "Bear" ? 1U : species == "Owl" ? 2U : 5U;
		CHECK(newParts.parts.at("tail").size() == expectedTailBones);
	}
}

TEST_CASE("Animal part examples resolve against the original GLBs", "[.avatar-source][parts]")
{
	const auto sourceRoot = core::env_var("BERNINI_AVATAR_SOURCE_ROOT");
	REQUIRE(sourceRoot.has_value());
	const auto files = core::file::LooseFileSystem("assets/avatar_parts");
	for (const std::string species : { "Bear", "Owl", "Cat" })
	{
		CAPTURE(species);
		const auto imported = loadFromGltf(
			std::filesystem::path(*sourceRoot) / (species + ".glb"),
			GltfLoadOptions{ .textures = GltfTextures::kSkip });
		const auto avatar   = loadAvatar(files, species + ".bavatar");
		const auto resolved = resolveAvatar(avatar, imported.skeleton);
		CHECK(resolved.parts.size() == 10);
		const auto store = StoreAt(std::filesystem::temp_directory_path());
		CHECK_THAT(
			store.Describe(avatar, &imported.skeleton),
			!Catch::Matchers::ContainsSubstring("INVALID"));
	}
}
