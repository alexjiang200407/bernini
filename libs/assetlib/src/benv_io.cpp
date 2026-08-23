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
			doc::take(json, "name", env.name, c_What);
			doc::take(json, "sky", env.sky, c_What);
			doc::take(json, "lighting", env.lighting, c_What);
			doc::take(json, "skyMipLevel", env.skyMipLevel, c_What);
			doc::take(json, "skyRotationY", env.skyRotationY, c_What);

			if (json.contains("exposureOverride"))
			{
				float authored = 0.0f;
				doc::take(json, "exposureOverride", authored, c_What);
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
			"benv: not a text document; if it is a chunk-era file, migrate the project with a "
			"build from before the schema removal");
		return envFromDocument(
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
	}

	void
	saveEnv(const BEnv& env, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeEnv(env), "benv");
	}

	BEnv
	loadEnv(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeEnv(bytes);
	}

	BEnv
	loadEnv(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeEnv(fileSystem.Read(path));
	}
}
