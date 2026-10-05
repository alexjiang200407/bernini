#include <array>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib_structs/BToonShadingRig.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <format>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "io/json_doc.h"

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		constexpr std::string_view c_EditsKey           = "edits";
		constexpr std::string_view c_KeysKey            = "keys";
		constexpr std::string_view c_NameKey            = "name";
		constexpr std::string_view c_KeySharpnessKey    = "keySharpness";
		constexpr std::string_view c_MirroredKey        = "mirrored";
		constexpr std::string_view c_FaceLightKey       = "faceLight";
		constexpr std::string_view c_FaceNormalKey      = "faceNormal";
		constexpr std::string_view c_SmoothingKey       = "smoothing";
		constexpr std::string_view c_RadiiKey           = "radii";
		constexpr std::string_view c_HeadBoneKey        = "headBone";
		constexpr std::string_view c_HeadToBoneKey      = "headToBone";
		constexpr std::string_view c_HeadRadiusKey      = "headRadius";
		constexpr std::string_view c_FadeStartPixelsKey = "fadeStartPixels";
		constexpr std::string_view c_FadeEndPixelsKey   = "fadeEndPixels";
		constexpr std::string_view c_LightKey           = "light";
		constexpr std::string_view c_PositionKey        = "position";

		/** The float fields of a key, by document name: one list for the reader and the writer. */
		struct KeyField
		{
			std::string_view key;
			float ToonShadingRigKey::* field;
		};

		constexpr std::array<KeyField, 9> c_KeyFields = { {
			{ "gain", &ToonShadingRigKey::gain },
			{ "size", &ToonShadingRigKey::size },
			{ "anisotropy", &ToonShadingRigKey::anisotropy },
			{ "sharpness", &ToonShadingRigKey::sharpness },
			{ "bend", &ToonShadingRigKey::bend },
			{ "bulge", &ToonShadingRigKey::bulge },
			{ "rotation", &ToonShadingRigKey::rotation },
			{ "radius", &ToonShadingRigKey::radius },
			{ "normalSmoothing", &ToonShadingRigKey::normalSmoothing },
		} };

		struct FaceLightField
		{
			std::string_view key;
			float ToonFaceLight::* field;
		};

		constexpr std::array<FaceLightField, 6> c_FaceLightFields = { {
			{ "minElevation", &ToonFaceLight::minElevation },
			{ "maxElevation", &ToonFaceLight::maxElevation },
			{ "maxAzimuth", &ToonFaceLight::maxAzimuth },
			{ "azimuthFadeStart", &ToonFaceLight::azimuthFadeStart },
			{ "azimuthFadeEnd", &ToonFaceLight::azimuthFadeEnd },
			{ "azimuthFadeAmount", &ToonFaceLight::azimuthFadeAmount },
		} };

		/** Takes the vector `key` out of `json`, which must hold it; `what` names the object. */
		void
		takeRequired(
			nlohmann::json&        json,
			const std::string_view key,
			glm::vec3&             out,
			const std::string_view what)
		{
			const auto it = json.find(key);
			if (it == json.end())
			{
				throw_runtime_error("{}: '{}' is required", what, key);
			}
			doc::vecFromJson(*it, key, out, what);
			json.erase(it);
		}

		ToonShadingRigKey
		keyFromJson(nlohmann::json& json, const std::string_view what)
		{
			if (!json.is_object())
			{
				throw_runtime_error("{} is not an object", what);
			}

			auto key = ToonShadingRigKey();
			takeRequired(json, c_LightKey, key.light, what);
			takeRequired(json, c_PositionKey, key.position, what);

			const doc::Taker taker(json, what);
			for (const auto& [name, field] : c_KeyFields) taker.Take(name, key.*field);

			key.extraJson = json.dump();
			return key;
		}

		ToonShadingRigEdit
		editFromJson(nlohmann::json& json, const size_t index)
		{
			const std::string what = std::format("btoonrig: edit {}", index);
			if (!json.is_object())
			{
				throw_runtime_error("{} is not an object", what);
			}

			auto edit = ToonShadingRigEdit();

			const doc::Taker taker(json, what);
			taker.Take(c_NameKey, edit.name);
			taker.Take(c_KeySharpnessKey, edit.keySharpness);
			taker.Take(c_MirroredKey, edit.mirrored);

			const auto keys = json.find(c_KeysKey);
			if (keys == json.end())
			{
				throw_runtime_error("{}: '{}' is required", what, c_KeysKey);
			}
			if (!keys->is_array())
			{
				throw_runtime_error("{}: '{}' is not an array", what, c_KeysKey);
			}
			for (size_t k = 0; k < keys->size(); ++k)
				edit.keys.push_back(keyFromJson((*keys)[k], std::format("{}, key {}", what, k)));
			json.erase(keys);

			edit.extraJson = json.dump();
			return edit;
		}

		glm::mat4
		matrixFromJson(const nlohmann::json& json)
		{
			if (!json.is_array() || json.size() != 4)
			{
				throw_runtime_error("btoonrig: '{}' is not four rows", c_HeadToBoneKey);
			}

			auto matrix = glm::mat4(1.0f);
			for (glm::length_t row = 0; row < 4; ++row)
			{
				auto values = glm::vec4(0.0f);
				doc::vecFromJson(
					json[static_cast<size_t>(row)],
					std::format("{} row {}", c_HeadToBoneKey, row),
					values,
					"btoonrig");
				for (glm::length_t column = 0; column < 4; ++column)
					matrix[column][row] = values[column];
			}
			return matrix;
		}

		nlohmann::json
		matrixToJson(const glm::mat4& matrix)
		{
			auto rows = nlohmann::json::array();
			for (glm::length_t row = 0; row < 4; ++row)
			{
				const auto values =
					glm::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
				rows.push_back(doc::vecToJson(values));
			}
			return rows;
		}
	}

	BToonShadingRig
	AssetCodec<BToonShadingRig>::Deserialize(std::span<const std::byte> bytes)
	{
		if (!isTextAssetDocument(bytes))
		{
			throw_runtime_error("btoonrig: the bytes are not a text document");
		}

		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		auto json = doc::parseObject(text, "btoonrig: the document");

		BToonShadingRig rig;

		const doc::Taker taker(json, "btoonrig");
		const bool       namesBone = json.contains(c_HeadBoneKey);
		taker.Take(c_HeadBoneKey, rig.headBone);
		if (namesBone && rig.headBone.empty())
		{
			throw_runtime_error("btoonrig: '{}' names no bone; omit it for none", c_HeadBoneKey);
		}
		taker.Take(c_HeadRadiusKey, rig.headRadius);
		taker.Take(c_FadeStartPixelsKey, rig.fadeStartPixels);
		taker.Take(c_FadeEndPixelsKey, rig.fadeEndPixels);

		if (const auto it = json.find(c_HeadToBoneKey); it != json.end())
		{
			rig.headToBone = matrixFromJson(*it);
			json.erase(it);
		}

		// The group's unknown keys stay in it, inside extraJson; the known ones are written back by
		// Serialize alone.
		if (const auto it = json.find(c_FaceLightKey); it != json.end())
		{
			if (!it->is_object())
			{
				throw_runtime_error("btoonrig: '{}' is not an object", c_FaceLightKey);
			}

			const doc::Taker group(*it, "btoonrig.faceLight");
			for (const auto& [name, field] : c_FaceLightFields)
				group.Take(name, rig.faceLight.*field);

			if (it->empty())
				json.erase(it);
		}

		if (const auto it = json.find(c_FaceNormalKey); it != json.end())
		{
			if (!it->is_object())
			{
				throw_runtime_error("btoonrig: '{}' is not an object", c_FaceNormalKey);
			}

			const doc::Taker group(*it, "btoonrig.faceNormal");
			group.Take(c_SmoothingKey, rig.faceNormal.smoothing);
			if (const auto radii = it->find(c_RadiiKey); radii != it->end())
			{
				doc::vecFromJson(*radii, c_RadiiKey, rig.faceNormal.radii, "btoonrig.faceNormal");
				it->erase(radii);
			}

			if (it->empty())
				json.erase(it);
		}

		if (const auto it = json.find(c_EditsKey); it != json.end())
		{
			if (!it->is_array())
			{
				throw_runtime_error("btoonrig: '{}' is not an array", c_EditsKey);
			}
			for (size_t i = 0; i < it->size(); ++i) rig.edits.push_back(editFromJson((*it)[i], i));
			json.erase(it);
		}

		rig.extraJson = json.dump();
		return rig;
	}

	std::vector<std::byte>
	AssetCodec<BToonShadingRig>::Serialize(const BToonShadingRig& rig)
	{
		auto json = doc::parseObject(rig.extraJson, "btoonrig: extraJson");

		auto edits = nlohmann::json::array();
		for (const ToonShadingRigEdit& edit : rig.edits)
		{
			auto object = doc::parseObject(edit.extraJson, "btoonrig: an edit's extraJson");

			auto keys = nlohmann::json::array();
			for (const ToonShadingRigKey& key : edit.keys)
			{
				auto entry        = doc::parseObject(key.extraJson, "btoonrig: a key's extraJson");
				entry[c_LightKey] = doc::vecToJson(key.light);
				entry[c_PositionKey] = doc::vecToJson(key.position);
				for (const auto& [name, field] : c_KeyFields)
					entry[name] = doc::plainFloat(key.*field);
				keys.push_back(std::move(entry));
			}

			if (!edit.name.empty())
				object[c_NameKey] = edit.name;
			object[c_KeySharpnessKey] = doc::plainFloat(edit.keySharpness);
			object[c_MirroredKey]     = edit.mirrored;
			object[c_KeysKey]         = std::move(keys);
			edits.push_back(std::move(object));
		}
		json[c_EditsKey] = std::move(edits);

		nlohmann::json& faceLight = json[c_FaceLightKey];
		if (!faceLight.is_object())
			faceLight = nlohmann::json::object();
		for (const auto& [name, field] : c_FaceLightFields)
			faceLight[name] = doc::plainFloat(rig.faceLight.*field);

		nlohmann::json& faceNormal = json[c_FaceNormalKey];
		if (!faceNormal.is_object())
			faceNormal = nlohmann::json::object();
		faceNormal[c_SmoothingKey] = doc::plainFloat(rig.faceNormal.smoothing);
		faceNormal[c_RadiiKey]     = doc::vecToJson(rig.faceNormal.radii);

		// Omitted rather than written empty: an absent bone and an empty name would otherwise be
		// two spellings of the placement's own frame.
		if (!rig.headBone.empty())
			json[c_HeadBoneKey] = rig.headBone;

		json[c_HeadToBoneKey]      = matrixToJson(rig.headToBone);
		json[c_HeadRadiusKey]      = doc::plainFloat(rig.headRadius);
		json[c_FadeStartPixelsKey] = doc::plainFloat(rig.fadeStartPixels);
		json[c_FadeEndPixelsKey]   = doc::plainFloat(rig.fadeEndPixels);

		return doc::toBytes(json);
	}
}
