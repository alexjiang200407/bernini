#include <assetlib/benvl_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "env_route_io.h"
#include "fs_util.h"
#include "string_io.h"

#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	using core::io::ByteReader;
	using core::io::ByteWriter;

	namespace
	{
		constexpr uint16_t c_VersionMajor = 2;

		// 1 appended the authored exposure override. Minor 0 files carry none, and read back with
		// none, which is exactly what "nobody has tuned this" means.
		constexpr uint16_t c_VersionMinor = 1;
	}

	std::vector<std::byte>
	serializeEnvLighting(const BEnvLighting& lighting)
	{
		ByteWriter writer;
		writer.WritePod(magic::c_BEnvL);
		writer.WritePod(c_VersionMajor);
		writer.WritePod(c_VersionMinor);

		writeString(writer, lighting.name);
		writeRoute(writer, lighting.prefilter);
		writeRoute(writer, lighting.irradiance);
		writer.WritePod(lighting.exposure);

		writer.WritePod(static_cast<uint8_t>(lighting.exposureOverride.has_value() ? 1 : 0));
		writer.WritePod(lighting.exposureOverride.value_or(0.0f));

		return writer.Take();
	}

	BEnvLighting
	deserializeEnvLighting(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);

		if (reader.ReadPod<uint32_t>() != magic::c_BEnvL)
			throw std::runtime_error("benvl: bad magic");

		const auto versionMajor = reader.ReadPod<uint16_t>();
		const auto versionMinor = reader.ReadPod<uint16_t>();

		if (versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"benvl: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_VersionMajor) + ")");

		BEnvLighting lighting;
		lighting.name       = readString(reader);
		lighting.prefilter  = readRoute(reader);
		lighting.irradiance = readRoute(reader);
		lighting.exposure   = reader.ReadPod<float>();

		// Minor versions are additive, so a newer writer's tail is read only when this file claims
		// to carry it -- and a minor 0 file simply has no authored exposure.
		if (versionMinor >= 1)
		{
			const auto authored = reader.ReadPod<uint8_t>();
			const auto value    = reader.ReadPod<float>();
			if (authored != 0)
				lighting.exposureOverride = value;
		}

		return lighting;
	}

	void
	saveEnvLighting(const BEnvLighting& lighting, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeEnvLighting(lighting), "benvl");
	}

	BEnvLighting
	loadEnvLighting(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeEnvLighting(bytes);
	}

	BEnvLighting
	loadEnvLighting(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeEnvLighting(fileSystem.Read(path));
	}
}
