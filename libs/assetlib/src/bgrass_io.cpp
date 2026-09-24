#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib_structs/BGrass.h>
#include <core/err/util.h>
#include <cstddef>
#include <format>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "json_doc.h"

namespace assetlib
{
	using core::throw_runtime_error_if;

	namespace
	{
		constexpr std::string_view c_MaterialKey = "material";
		constexpr std::string_view c_BladeKey    = "blade";
		constexpr std::string_view c_ClumpKey    = "clump";
		constexpr std::string_view c_DensityKey  = "density";
		constexpr std::string_view c_ResponseKey = "response";
		constexpr std::string_view c_LightingKey = "lighting";
		constexpr std::string_view c_ColorKey    = "color";

		/**
		 * Takes the known keys of group `key` out of `json` through `read`, leaving the unknown ones
		 * inside the group where they were. A group emptied by that is dropped, so the known keys
		 * are written back by Serialize alone and never twice.
		 */
		template <typename Read>
		void
		takeGroup(nlohmann::json& json, const std::string_view key, Read&& read)
		{
			const auto it = json.find(key);
			if (it == json.end())
			{
				return;
			}

			throw_runtime_error_if(!it->is_object(), "bgrass: '{}' is not an object", key);

			const std::string what = std::format("bgrass.{}", key);
			const doc::Taker  taker(*it, what);
			read(taker);

			if (it->empty())
			{
				json.erase(it);
			}
		}

		/** The group `key` of `json`, made an object if the unknown keys held none. */
		[[nodiscard]] nlohmann::json&
		groupOf(nlohmann::json& json, const std::string_view key)
		{
			nlohmann::json& group = json[key];
			if (!group.is_object())
			{
				group = nlohmann::json::object();
			}
			return group;
		}
	}

	BGrass
	AssetCodec<BGrass>::Deserialize(std::span<const std::byte> bytes)
	{
		throw_runtime_error_if(
			!isTextAssetDocument(bytes),
			"bgrass: the bytes are not a text document");

		const auto text =
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		auto json = doc::parseObject(text, "bgrass: the document");

		BGrass grass;
		doc::Taker(json, "bgrass").Take(c_MaterialKey, grass.material);

		takeGroup(json, c_BladeKey, [&grass](const doc::Taker& taker) {
			GrassBladeParams& blade = grass.blade;
			taker.Take("minHeight", blade.minHeight);
			taker.Take("maxHeight", blade.maxHeight);
			taker.Take("rootWidth", blade.rootWidth);
			taker.Take("tipWidth", blade.tipWidth);
			taker.Take("curvature", blade.curvature);
			taker.Take("lean", blade.lean);
			taker.Take("nearSegments", blade.nearSegments);
			taker.Take("farSegments", blade.farSegments);
		});

		takeGroup(json, c_ClumpKey, [&grass](const doc::Taker& taker) {
			taker.Take("bladesPerClump", grass.clump.bladesPerClump);
			taker.Take("radius", grass.clump.radius);
		});

		takeGroup(json, c_DensityKey, [&grass](const doc::Taker& taker) {
			taker.Take("fadeStart", grass.density.fadeStart);
			taker.Take("fadeEnd", grass.density.fadeEnd);
			taker.Take("widening", grass.density.widening);
		});

		takeGroup(json, c_ResponseKey, [&grass](const doc::Taker& taker) {
			taker.Take("stiffness", grass.response.stiffness);
			taker.Take("gustResponse", grass.response.gustResponse);
		});

		takeGroup(json, c_LightingKey, [&grass](const doc::Taker& taker) {
			GrassLightingParams& lighting = grass.lighting;
			taker.Take("rootOcclusion", lighting.rootOcclusion);
			taker.Take("normalRounding", lighting.normalRounding);
			taker.Take("groundNormalNear", lighting.groundNormalNear);
			taker.Take("groundNormalFar", lighting.groundNormalFar);
			taker.Take("translucencyColor", lighting.translucencyColor);
			taker.Take("translucency", lighting.translucency);
		});

		takeGroup(json, c_ColorKey, [&grass](const doc::Taker& taker) {
			taker.Take("rootTint", grass.color.rootTint);
			taker.Take("tipTint", grass.color.tipTint);
			taker.Take("variation", grass.color.variation);
		});

		grass.extraJson = json.dump();
		return grass;
	}

	std::vector<std::byte>
	AssetCodec<BGrass>::Serialize(const BGrass& grass)
	{
		auto json = doc::parseObject(grass.extraJson, "bgrass: extraJson");

		json[c_MaterialKey] = grass.material;

		nlohmann::json& blade = groupOf(json, c_BladeKey);
		blade["minHeight"]    = doc::plainFloat(grass.blade.minHeight);
		blade["maxHeight"]    = doc::plainFloat(grass.blade.maxHeight);
		blade["rootWidth"]    = doc::plainFloat(grass.blade.rootWidth);
		blade["tipWidth"]     = doc::plainFloat(grass.blade.tipWidth);
		blade["curvature"]    = doc::plainFloat(grass.blade.curvature);
		blade["lean"]         = doc::plainFloat(grass.blade.lean);
		blade["nearSegments"] = grass.blade.nearSegments;
		blade["farSegments"]  = grass.blade.farSegments;

		nlohmann::json& clump   = groupOf(json, c_ClumpKey);
		clump["bladesPerClump"] = grass.clump.bladesPerClump;
		clump["radius"]         = doc::plainFloat(grass.clump.radius);

		nlohmann::json& density = groupOf(json, c_DensityKey);
		density["fadeStart"]    = doc::plainFloat(grass.density.fadeStart);
		density["fadeEnd"]      = doc::plainFloat(grass.density.fadeEnd);
		density["widening"]     = doc::plainFloat(grass.density.widening);

		nlohmann::json& response = groupOf(json, c_ResponseKey);
		response["stiffness"]    = doc::plainFloat(grass.response.stiffness);
		response["gustResponse"] = doc::plainFloat(grass.response.gustResponse);

		nlohmann::json& lighting      = groupOf(json, c_LightingKey);
		lighting["rootOcclusion"]     = doc::plainFloat(grass.lighting.rootOcclusion);
		lighting["normalRounding"]    = doc::plainFloat(grass.lighting.normalRounding);
		lighting["groundNormalNear"]  = doc::plainFloat(grass.lighting.groundNormalNear);
		lighting["groundNormalFar"]   = doc::plainFloat(grass.lighting.groundNormalFar);
		lighting["translucencyColor"] = doc::vecToJson(grass.lighting.translucencyColor);
		lighting["translucency"]      = doc::plainFloat(grass.lighting.translucency);

		nlohmann::json& color = groupOf(json, c_ColorKey);
		color["rootTint"]     = doc::vecToJson(grass.color.rootTint);
		color["tipTint"]      = doc::vecToJson(grass.color.tipTint);
		color["variation"]    = doc::plainFloat(grass.color.variation);

		return doc::toBytes(json);
	}
}
