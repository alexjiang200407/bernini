#include <assetlib/avatar.h>
#include <assetlib/codecs.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>

#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "json_doc.h"
#include "ref_paths.h"
#include <algorithm>
#include <cmath>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_LegsKey  = "legs";
		constexpr std::string_view c_PlantKey = "plant";
		constexpr std::string_view c_PartsKey = "parts";
		constexpr std::string_view c_StartKey = "start";
		constexpr std::string_view c_EndKey   = "end";

		void
		validatePart(std::string_view name, const AvatarPart& part)
		{
			core::throw_runtime_error_if(name.empty(), "avatar: a part has an empty name");
			core::throw_runtime_error_if(
				part.startBoneName.empty() || part.endBoneName.empty(),
				"avatar: part '{}' requires nonempty start and end bone names",
				name);
		}

		void
		resolveParts(const Avatar& avatar, const Skeleton& skeleton, ResolvedAvatar& out)
		{
			if (avatar.parts.empty())
				return;
			validateSkeleton(skeleton);
			std::unordered_map<std::string_view, uint32_t> indices;
			indices.reserve(skeleton.bones.size());
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const auto [it, inserted] = indices.emplace(
					skeleton.stringPool.at(skeleton.bones[i].nameOffset),
					static_cast<uint32_t>(i));
				if (!inserted)
					it->second = c_InvalidIndex;
			}

			for (const auto& [name, part] : avatar.parts)
			{
				validatePart(name, part);
				const auto indexOf = [&](std::string_view bone) {
					const auto it = indices.find(bone);
					core::throw_runtime_error_if(
						it == indices.end(),
						"avatar: part '{}' names bone '{}', which the skeleton does not carry",
						name,
						bone);
					core::throw_runtime_error_if(
						it->second == c_InvalidIndex,
						"avatar: part '{}' names ambiguous bone '{}'",
						name,
						bone);
					return it->second;
				};
				const uint32_t start = indexOf(part.startBoneName);
				const uint32_t end   = indexOf(part.endBoneName);
				auto&          chain = out.parts[name];
				for (uint32_t bone = end;; bone = skeleton.bones[bone].parent)
				{
					core::throw_runtime_error_if(
						bone == c_InvalidIndex,
						"avatar: part '{}' end '{}' is not descended from start '{}'",
						name,
						part.endBoneName,
						part.startBoneName);
					chain.push_back(bone);
					if (bone == start)
						break;
				}
				std::ranges::reverse(chain);
			}
		}
		// Read and never written: the list this key was before it became a weight, kept so a
		// document from then still loads. A save rewrites it as `plant`.
		constexpr std::string_view c_UnplantedKey = "unplanted";
		constexpr std::string_view c_HipKey       = "hip";
		constexpr std::string_view c_KneeKey      = "knee";
		constexpr std::string_view c_AnkleKey     = "ankle";
		constexpr std::string_view c_ToeKey       = "toe";

		void
		takeBone(const nlohmann::json& leg, std::string_view key, std::string& out, size_t index)
		{
			const auto it = leg.find(key);
			core::throw_runtime_error_if(
				it == leg.end() || !it->is_string() || it->get<std::string>().empty(),
				"avatar: leg {} has no '{}' bone name",
				index,
				key);
			out = it->get<std::string>();
		}

		void
		addClipWeight(Avatar& avatar, std::string clip, float weight, std::string_view key)
		{
			core::throw_runtime_error_if(clip.empty(), "avatar: '{}' names an empty clip", key);
			core::throw_runtime_error_if(
				!std::isfinite(weight) || weight < 0.0f || weight > 1.0f,
				"avatar: '{}' weights '{}' at {}, outside 0 to 1",
				key,
				clip,
				weight);
			for (const ClipPlantWeight& entry : avatar.clipWeights)
				core::throw_runtime_error_if(
					entry.clip == clip,
					"avatar: '{}' is weighted twice",
					clip);
			// Negative zero passes the range check and would print and hash as its own value.
			avatar.clipWeights.emplace_back(std::move(clip), weight == 0.0f ? 0.0f : weight);
		}

		uint32_t
		boneIndex(
			const Skeleton&  skeleton,
			std::string_view name,
			size_t           leg,
			std::string_view joint)
		{
			const std::optional<uint32_t> found = findBone(skeleton, name);
			core::throw_runtime_error_if(
				!found.has_value(),
				"avatar: leg {}'s {} names bone '{}', which the skeleton does not carry",
				leg,
				joint,
				name);
			return *found;
		}
	}

	std::string
	avatarKeyFor(std::string_view skeletonKey)
	{
		return swapHalf(
			"avatar",
			skeletonKey,
			{ c_SkeletonsDirectoryName, c_SkeletonExtension },
			{ c_AvatarsDirectoryName, c_AvatarExtension });
	}

	std::string
	skeletonKeyForAvatar(std::string_view avatarKey)
	{
		return swapHalf(
			"avatar",
			avatarKey,
			{ c_AvatarsDirectoryName, c_AvatarExtension },
			{ c_SkeletonsDirectoryName, c_SkeletonExtension });
	}

	ResolvedAvatar
	resolveAvatar(const Avatar& avatar, const Skeleton& skeleton)
	{
		auto out = ResolvedAvatar();
		out.legs.reserve(avatar.legs.size());

		for (size_t i = 0; i < avatar.legs.size(); ++i)
		{
			const AvatarLeg& leg = avatar.legs[i];
			out.legs.push_back(
				{ boneIndex(skeleton, leg.hipBoneName, i, c_HipKey),
			      boneIndex(skeleton, leg.kneeBoneName, i, c_KneeKey),
			      boneIndex(skeleton, leg.ankleBoneName, i, c_AnkleKey),
			      boneIndex(skeleton, leg.toeBoneName, i, c_ToeKey) });
		}

		out.clipWeights = avatar.clipWeights;
		resolveParts(avatar, skeleton, out);
		return out;
	}

	Avatar
	loadAvatar(const core::file::IFileSystem& files, std::string_view key)
	{
		const std::vector<std::byte> bytes = files.Read(key);
		return AssetCodec<Avatar>::Deserialize(bytes);
	}

	ResolvedAvatar
	avatarForRig(
		const core::file::IFileSystem& files,
		const std::string_view         skeletonKey,
		const Skeleton&                skeleton)
	{
		if (skeletonKey.empty())
			return {};

		std::string key;
		try
		{
			key = avatarKeyFor(skeletonKey);
		}
		catch (const std::exception&)
		{
			// A skeleton outside the skeletons directory: nothing here can act on it, and the
			// reference scan is where a misplaced container gets reported.
			return {};
		}

		if (!files.Stat(key).has_value())
			return {};

		try
		{
			return resolveAvatar(loadAvatar(files, key), skeleton);
		}
		catch (const std::exception& e)
		{
			spdlog::warn(
				"'{}' cannot be used against '{}', so the rig has no avatar parts or foot "
				"planting: {}",
				key,
				skeletonKey,
				e.what());
			return {};
		}
	}

	Avatar
	AssetCodec<Avatar>::Deserialize(std::span<const std::byte> bytes)
	{
		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());

		auto json = doc::parseObject(text, "avatar: the document");

		Avatar avatar;

		if (auto it = json.find(c_PartsKey); it != json.end())
		{
			core::throw_runtime_error_if(!it->is_object(), "avatar: 'parts' is not an object");
			for (const auto& [name, value] : it->items())
			{
				auto part = AvatarPart();
				if (value.is_string())
					part.startBoneName = part.endBoneName = value.get<std::string>();
				else
				{
					core::throw_runtime_error_if(
						!value.is_object(),
						"avatar: part '{}' is not a bone name or start/end object",
						name);
					for (const auto key : { c_StartKey, c_EndKey })
						core::throw_runtime_error_if(
							!value.contains(key) || !value[key].is_string(),
							"avatar: part '{}' requires a '{}' bone name",
							name,
							key);
					part.startBoneName = value[c_StartKey].get<std::string>();
					part.endBoneName   = value[c_EndKey].get<std::string>();
					auto extra         = value;
					extra.erase(c_StartKey);
					extra.erase(c_EndKey);
					part.extraJson = extra.dump();
				}
				validatePart(name, part);
				avatar.parts.emplace(name, std::move(part));
			}
			json.erase(it);
		}

		if (auto it = json.find(c_LegsKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_array(),
				"avatar: '{}' is not an array",
				c_LegsKey);

			for (size_t i = 0; i < it->size(); ++i)
			{
				const nlohmann::json& leg = (*it)[i];
				core::throw_runtime_error_if(
					!leg.is_object(),
					"avatar: leg {} is not an object",
					i);

				auto chain = AvatarLeg();
				takeBone(leg, c_HipKey, chain.hipBoneName, i);
				takeBone(leg, c_KneeKey, chain.kneeBoneName, i);
				takeBone(leg, c_AnkleKey, chain.ankleBoneName, i);
				takeBone(leg, c_ToeKey, chain.toeBoneName, i);
				avatar.legs.push_back(std::move(chain));
			}
			json.erase(it);
		}

		if (auto it = json.find(c_PlantKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_object(),
				"avatar: '{}' is not an object of clip name to weight",
				c_PlantKey);

			for (const auto& [clip, weight] : it->items())
			{
				core::throw_runtime_error_if(
					!weight.is_number(),
					"avatar: '{}' weights '{}' with something that is not a number",
					c_PlantKey,
					clip);
				addClipWeight(avatar, clip, weight.get<float>(), c_PlantKey);
			}
			json.erase(it);
		}

		if (auto it = json.find(c_UnplantedKey); it != json.end())
		{
			core::throw_runtime_error_if(
				!it->is_array(),
				"avatar: '{}' is not an array",
				c_UnplantedKey);

			for (size_t i = 0; i < it->size(); ++i)
			{
				const nlohmann::json& name = (*it)[i];
				core::throw_runtime_error_if(
					!name.is_string(),
					"avatar: '{}' entry {} is not a clip name",
					c_UnplantedKey,
					i);
				addClipWeight(avatar, name.get<std::string>(), 0.0f, c_UnplantedKey);
			}
			json.erase(it);
		}

		std::ranges::sort(avatar.clipWeights, {}, &ClipPlantWeight::clip);

		avatar.extraJson = json.dump();
		return avatar;
	}

	std::vector<std::byte>
	AssetCodec<Avatar>::Serialize(const Avatar& avatar)
	{
		auto json = doc::parseObject(avatar.extraJson, "avatar: extraJson");

		json.erase(c_PartsKey);
		if (!avatar.parts.empty())
		{
			auto parts = nlohmann::json::object();
			for (const auto& [name, part] : avatar.parts)
			{
				validatePart(name, part);
				auto value = doc::parseObject(part.extraJson, "avatar part: extraJson");
				value.erase(c_StartKey);
				value.erase(c_EndKey);
				if (part.startBoneName == part.endBoneName && value.empty())
					parts[name] = part.startBoneName;
				else
				{
					value[c_StartKey] = part.startBoneName;
					value[c_EndKey]   = part.endBoneName;
					parts[name]       = std::move(value);
				}
			}
			json[c_PartsKey] = std::move(parts);
		}

		auto legs = nlohmann::json::array();
		for (const AvatarLeg& leg : avatar.legs)
		{
			legs.push_back(
				nlohmann::json{ { c_HipKey, leg.hipBoneName },
			                    { c_KneeKey, leg.kneeBoneName },
			                    { c_AnkleKey, leg.ankleBoneName },
			                    { c_ToeKey, leg.toeBoneName } });
		}

		// Written even when empty: an avatar with no legs is what the editor's *Create avatar*
		// writes, and a document with no keys at all reads as one nobody has opened yet.
		json[c_LegsKey] = std::move(legs);

		// Absent when empty, unlike `legs`: the key is an exception to a rule, and a document that
		// names none reads as one with none. The list it used to be is never written back.
		json.erase(c_UnplantedKey);
		json.erase(c_PlantKey);
		if (!avatar.clipWeights.empty())
		{
			auto plant = nlohmann::json::object();
			for (const ClipPlantWeight& entry : avatar.clipWeights)
				plant[entry.clip] = doc::plainFloat(entry.weight);
			json[c_PlantKey] = std::move(plant);
		}

		return doc::toBytes(json);
	}
}
