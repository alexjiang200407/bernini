#include <assetlib/avatar.h>
#include <assetlib/codecs.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>

#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json_doc.h"
#include "ref_paths.h"
#include <core/file/IFileSystem.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_LegsKey      = "legs";
		constexpr std::string_view c_UnplantedKey = "unplanted";
		constexpr std::string_view c_HipKey       = "hip";
		constexpr std::string_view c_KneeKey      = "knee";
		constexpr std::string_view c_AnkleKey     = "ankle";
		constexpr std::string_view c_ToeKey       = "toe";

		/**
		 * `from`'s tail beneath `fromDirectory`, re-rooted at `toDirectory` with `extension` in
		 * place of its own -- the whole of the convention, written once so the two directions
		 * cannot disagree about it.
		 */
		std::string
		swapHalf(
			std::string_view from,
			std::string_view fromDirectory,
			std::string_view toDirectory,
			std::string_view fromExtension,
			std::string_view toExtension)
		{
			const std::string key = normalizePath(from);

			core::throw_runtime_error_if(
				extensionOf(key) != fromExtension,
				"avatar: '{}' is not a '{}'",
				from,
				fromExtension);

			core::throw_runtime_error_if(
				!isUnder(key, fromDirectory),
				"avatar: '{}' is not under '{}'",
				from,
				fromDirectory);

			const std::string_view tail = std::string_view(key).substr(
				fromDirectory.size(),
				key.size() - fromDirectory.size() - fromExtension.size());

			return std::string(toDirectory).append(tail).append(toExtension);
		}

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
			skeletonKey,
			c_SkeletonsDirectoryName,
			c_AvatarsDirectoryName,
			c_SkeletonExtension,
			c_AvatarExtension);
	}

	std::string
	skeletonKeyForAvatar(std::string_view avatarKey)
	{
		return swapHalf(
			avatarKey,
			c_AvatarsDirectoryName,
			c_SkeletonsDirectoryName,
			c_AvatarExtension,
			c_SkeletonExtension);
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

		out.unplantedClips = avatar.unplantedClips;
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
				"'{}' cannot be used against '{}', so the rig plants no feet: {}",
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
					!name.is_string() || name.get<std::string>().empty(),
					"avatar: '{}' entry {} is not a clip name",
					c_UnplantedKey,
					i);
				avatar.unplantedClips.push_back(name.get<std::string>());
			}
			json.erase(it);
		}

		avatar.extraJson = json.dump();
		return avatar;
	}

	std::vector<std::byte>
	AssetCodec<Avatar>::Serialize(const Avatar& avatar)
	{
		auto json = doc::parseObject(avatar.extraJson, "avatar: extraJson");

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
		// lists none reads as one with none.
		json.erase(c_UnplantedKey);
		if (!avatar.unplantedClips.empty())
			json[c_UnplantedKey] = avatar.unplantedClips;

		return doc::toBytes(json);
	}
}
