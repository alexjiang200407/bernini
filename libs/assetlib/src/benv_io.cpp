#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib_structs/BEnv.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <nlohmann/json.hpp>

#include "fs_util.h"
#include "json_doc.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "benv";

		BEnv
		envFromDocument(std::string_view text)
		{
			auto json = doc::parseObject(text, "benv: the document");

			BEnv env;

			const doc::Taker taker(json, c_What);

			// The one model there is; refused rather than defaulted when it names another, since a
			// typo that silently lit as PBR would never be found.
			std::string shadingModel = "pbr";
			taker.Take("shadingModel", shadingModel);
			core::throw_runtime_error_if(
				shadingModel != "pbr",
				"benv: unknown shading model '{}'",
				shadingModel);
			env.shadingModel = ShadingModel::kPbr;

			taker.Take("name", env.name);
			taker.Take("sky", env.sky);
			taker.Take("skyMipLevel", env.skyMipLevel);
			taker.Take("skyRotationY", env.skyRotationY);

			// A document from before the shading model named its lighting at the top level. Unknown
			// keys are preserved rather than refused, so left to itself this one would ride
			// `extraJson`, resolve unlit, and be written back that way by the next save.
			core::throw_runtime_error_if(
				json.contains("lighting") || json.contains("exposureOverride"),
				"benv: 'lighting' and 'exposureOverride' belong under 'pbr' -- re-author this "
				"document");

			if (const auto it = json.find("rim"); it != json.end())
			{
				core::throw_runtime_error_if(!it->is_object(), "benv: 'rim' is not an object");

				const doc::Taker rim(*it, c_What);
				rim.Take("tint", env.rim.tint);
				rim.Take("intensity", env.rim.intensity);
				rim.Take("power", env.rim.power);
				if (it->empty())
					json.erase(it);
			}

			if (const auto it = json.find("pbr"); it != json.end())
			{
				core::throw_runtime_error_if(!it->is_object(), "benv: 'pbr' is not an object");

				const doc::Taker pbr(*it, c_What);
				pbr.Take("lighting", env.pbr.lighting);

				if (it->contains("exposureOverride"))
				{
					float authored = 0.0f;
					pbr.Take("exposureOverride", authored);
					env.pbr.exposureOverride = authored;
				}
				if (it->empty())
					json.erase(it);
			}

			env.extraJson = json.dump();
			return env;
		}
	}

	std::vector<std::byte>
	AssetCodec<BEnv>::Serialize(const BEnv& env)
	{
		auto json = doc::parseObject(env.extraJson, "benv: extraJson");

		// No default, so a new model cannot be added without the compiler pointing here.
		switch (env.shadingModel)
		{
		case ShadingModel::kPbr:
			json["shadingModel"] = "pbr";
			break;
		case ShadingModel::kCount:
			throw std::runtime_error("benv: unwritable shading model");
		}

		json["name"]         = env.name;
		json["sky"]          = env.sky;
		json["skyMipLevel"]  = env.skyMipLevel;
		json["skyRotationY"] = doc::plainFloat(env.skyRotationY);

		// Merged into whatever `extraJson` preserved rather than rebuilt, so a sibling branch's key
		// inside either block survives this writer too.
		nlohmann::json& rim = json["rim"];
		rim["tint"]         = doc::vecToJson(env.rim.tint);
		rim["intensity"]    = doc::plainFloat(env.rim.intensity);
		rim["power"]        = doc::plainFloat(env.rim.power);

		nlohmann::json& pbr = json["pbr"];
		pbr["lighting"]     = env.pbr.lighting;

		if (env.pbr.exposureOverride.has_value())
			pbr["exposureOverride"] = doc::plainFloat(*env.pbr.exposureOverride);
		else
			pbr.erase("exposureOverride");

		return doc::toBytes(json);
	}

	BEnv
	AssetCodec<BEnv>::Deserialize(std::span<const std::byte> bytes)
	{
		core::throw_runtime_error_if(
			!isTextAssetDocument(bytes),
			"benv: not a text document; a chunk-era file is no longer convertible -- "
			"re-import the environment");
		return envFromDocument(
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	}

}
