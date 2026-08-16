#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "env_route_io.h"
#include "fs_util.h"
#include "string_io.h"

#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

#include "mounted_io.h"

namespace assetlib
{
	using core::io::ByteReader;
	using core::io::ByteWriter;

	namespace
	{
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;
	}

	std::vector<std::byte>
	serializeSky(const BSky& sky)
	{
		ByteWriter writer;
		writer.WritePod(magic::c_BSky);
		writer.WritePod(c_VersionMajor);
		writer.WritePod(c_VersionMinor);

		writeString(writer, sky.name);
		writeRoute(writer, sky.sky);
		writer.WritePod(sky.mipLevel);
		writer.WritePod(sky.rotationY);

		return writer.Take();
	}

	BSky
	deserializeSky(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);

		if (reader.ReadPod<uint32_t>() != magic::c_BSky)
			throw std::runtime_error("bsky: bad magic");

		const auto versionMajor = reader.ReadPod<uint16_t>();
		// The minor version is additive within a major, and nothing here is optional yet.
		static_cast<void>(reader.ReadPod<uint16_t>());

		if (versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"bsky: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_VersionMajor) + ")");

		BSky sky;
		sky.name      = readString(reader);
		sky.sky       = readRoute(reader);
		sky.mipLevel  = reader.ReadPod<uint32_t>();
		sky.rotationY = reader.ReadPod<float>();
		return sky;
	}

	void
	saveSky(const BSky& sky, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeSky(sky), "bsky");
	}

	BSky
	loadSky(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeSky(bytes);
	}

	BSky
	loadSky(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeSky(fileSystem.Read(path));
	}
}
