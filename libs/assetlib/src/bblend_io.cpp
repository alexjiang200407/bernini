#include <assetlib/blend.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <core/err/util.h>
#include <nlohmann/json.hpp>

#include "json_doc.h"

namespace assetlib
{
	using core::throw_runtime_error;
	using core::throw_runtime_error_if;

	namespace
	{
		constexpr std::string_view c_What = "bblend";

		constexpr std::string_view c_NameKey       = "name";
		constexpr std::string_view c_AnimationsKey = "animations";
		constexpr std::string_view c_SpacesKey     = "spaces";
		constexpr std::string_view c_MembersKey    = "members";
		constexpr std::string_view c_ClipKey       = "clip";
		constexpr std::string_view c_ParameterKey  = "parameter";

		BlendSpaceMember
		memberFromJson(const nlohmann::json& json, size_t space, size_t index)
		{
			throw_runtime_error_if(
				!json.is_object(),
				"bblend: member {} of space {} is not an object",
				index,
				space);

			auto member = BlendSpaceMember();

			const auto clip = json.find(c_ClipKey);
			throw_runtime_error_if(
				clip == json.end() || !clip->is_string() || clip->get<std::string>().empty(),
				"bblend: member {} of space {} names no clip",
				index,
				space);
			member.clip = clip->get<std::string>();

			const auto parameter = json.find(c_ParameterKey);
			throw_runtime_error_if(
				parameter == json.end() || !parameter->is_number(),
				"bblend: member {} of space {} has no numeric '{}'",
				index,
				space,
				c_ParameterKey);
			member.parameter = parameter->get<float>();

			return member;
		}

		BlendSpace
		spaceFromJson(const nlohmann::json& json, size_t index)
		{
			throw_runtime_error_if(!json.is_object(), "bblend: space {} is not an object", index);

			auto space = BlendSpace();

			const auto name = json.find(c_NameKey);
			throw_runtime_error_if(
				name == json.end() || !name->is_string() || name->get<std::string>().empty(),
				"bblend: space {} is unnamed",
				index);
			space.name = name->get<std::string>();

			const auto members = json.find(c_MembersKey);
			throw_runtime_error_if(
				members == json.end() || !members->is_array(),
				"bblend: space '{}' has no '{}' array",
				space.name,
				c_MembersKey);

			for (size_t i = 0; i < members->size(); ++i)
				space.members.push_back(memberFromJson((*members)[i], index, i));

			return space;
		}
	}

	void
	validateBlendSet(const BlendSet& set)
	{
		// Only once there is a space to resolve: a set with neither is the empty document a
		// *create* writes, the way an avatar with no legs is, and refusing that would refuse the
		// file before anybody has authored it.
		if (!set.spaces.empty() && set.animations.empty())
			throw_runtime_error(
				"blend set: {} spaces name clips of no clip set",
				set.spaces.size());

		for (size_t s = 0; s < set.spaces.size(); ++s)
		{
			const BlendSpace& space = set.spaces[s];

			if (space.name.empty())
				throw_runtime_error("blend set: space {} is unnamed", s);

			for (size_t other = 0; other < s; ++other)
				if (set.spaces[other].name == space.name)
					throw_runtime_error("blend set: two spaces are named '{}'", space.name);

			// Two is the floor because a one-member space is a clip, and a clip is already a node
			// under its own name -- an authored one would be a second name for the same thing.
			if (space.members.size() < 2)
				throw_runtime_error(
					"blend set: space '{}' holds {} members, and a blend space needs at least two",
					space.name,
					space.members.size());

			for (size_t m = 0; m < space.members.size(); ++m)
			{
				const BlendSpaceMember& member = space.members[m];

				if (member.clip.empty())
					throw_runtime_error(
						"blend set: member {} of space '{}' names no clip",
						m,
						space.name);

				if (!std::isfinite(member.parameter))
					throw_runtime_error(
						"blend set: member {} of space '{}' has a parameter of {}",
						m,
						space.name,
						member.parameter);

				// Strictly increasing, not merely sorted: two members at one parameter have no
				// defined weighting between them, and the span between them is a divisor.
				if (m > 0 && !(member.parameter > space.members[m - 1].parameter))
					throw_runtime_error(
						"blend set: space '{}' has parameter {} at member {} after {}, and they "
						"must strictly increase",
						space.name,
						member.parameter,
						m,
						space.members[m - 1].parameter);
			}
		}
	}

	BlendSet
	AssetCodec<BlendSet>::Deserialize(std::span<const std::byte> bytes)
	{
		throw_runtime_error_if(
			!isTextAssetDocument(bytes),
			"bblend: the bytes are not a text document");

		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());

		auto json = doc::parseObject(text, "bblend: the document");

		BlendSet set;

		const doc::Taker taker(json, c_What);
		taker.Take(c_NameKey, set.name);
		taker.Take(c_AnimationsKey, set.animations);

		if (auto it = json.find(c_SpacesKey); it != json.end())
		{
			throw_runtime_error_if(!it->is_array(), "bblend: '{}' is not an array", c_SpacesKey);

			for (size_t i = 0; i < it->size(); ++i)
				set.spaces.push_back(spaceFromJson((*it)[i], i));

			json.erase(it);
		}

		set.extraJson = json.dump();

		validateBlendSet(set);
		return set;
	}

	std::vector<std::byte>
	AssetCodec<BlendSet>::Serialize(const BlendSet& set)
	{
		validateBlendSet(set);

		auto json = doc::parseObject(set.extraJson, "bblend: extraJson");

		json[c_NameKey]       = set.name;
		json[c_AnimationsKey] = set.animations;

		auto spaces = nlohmann::json::array();
		for (const BlendSpace& space : set.spaces)
		{
			auto members = nlohmann::json::array();
			for (const BlendSpaceMember& member : space.members)
			{
				members.push_back(
					nlohmann::json{ { c_ClipKey, member.clip },
				                    { c_ParameterKey, doc::plainFloat(member.parameter) } });
			}

			spaces.push_back(
				nlohmann::json{ { c_NameKey, space.name }, { c_MembersKey, std::move(members) } });
		}

		// Written even when empty, like an avatar's legs: a document with no keys at all reads as
		// one nobody has authored yet, rather than as a set with no spaces.
		json[c_SpacesKey] = std::move(spaces);

		return doc::toBytes(json);
	}
}
