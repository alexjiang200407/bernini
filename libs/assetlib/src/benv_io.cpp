#include "assetlib/benv_io.h"

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
			taker.Take("name", env.name);
			taker.Take("sky", env.sky);
			taker.Take("lighting", env.lighting);
			taker.Take("skyMipLevel", env.skyMipLevel);
			taker.Take("skyRotationY", env.skyRotationY);

			if (json.contains("exposureOverride"))
			{
				float authored = 0.0f;
				taker.Take("exposureOverride", authored);
				env.exposureOverride = authored;
			}

			env.extraJson = json.dump();
			return env;
		}
	}

	std::vector<std::byte>
	serializeEnv(const BEnv& env)
	{
		auto json = doc::parseObject(env.extraJson, "benv: extraJson");

		json["name"]         = env.name;
		json["sky"]          = env.sky;
		json["lighting"]     = env.lighting;
		json["skyMipLevel"]  = env.skyMipLevel;
		json["skyRotationY"] = doc::plainFloat(env.skyRotationY);

		if (env.exposureOverride.has_value())
			json["exposureOverride"] = doc::plainFloat(*env.exposureOverride);
		else
			json.erase("exposureOverride");

		return doc::toBytes(json);
	}

	BEnv
	deserializeEnv(std::span<const std::byte> bytes)
	{
		core::throw_runtime_error_if(
			!isTextAssetDocument(bytes),
			"benv: not a text document; a chunk-era file is no longer convertible -- "
			"re-import the environment");
		return envFromDocument(
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	}

	std::vector<std::byte>
	AssetCodec<BEnv>::Serialize(const BEnv& value)
	{
		return serializeEnv(value);
	}

	BEnv
	AssetCodec<BEnv>::Deserialize(std::span<const std::byte> bytes)
	{
		return deserializeEnv(bytes);
	}
}
