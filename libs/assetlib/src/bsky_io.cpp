#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include "env_route_io.h"
#include "fs_util.h"

#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	using core::io::ByteReader;
	using core::io::ByteWriter;

	namespace
	{
		constexpr uint32_t c_Magic = magic::c_BSky;

		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;
	}

	std::vector<std::byte>
	serializeSky(const BSky& sky)
	{
		ByteWriter writer;
		writer.writePod(c_Magic);
		writer.writePod(c_VersionMajor);
		writer.writePod(c_VersionMinor);

		writeString(writer, sky.name);
		writeRoute(writer, sky.sky);
		writer.writePod(sky.mipLevel);
		writer.writePod(sky.rotationY);

		return writer.take();
	}

	BSky
	deserializeSky(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);

		if (reader.readPod<uint32_t>() != c_Magic)
			throw std::runtime_error("bsky: bad magic");

		const auto versionMajor = reader.readPod<uint16_t>();
		// The minor version is additive within a major, and nothing here is optional yet.
		static_cast<void>(reader.readPod<uint16_t>());

		if (versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"bsky: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_VersionMajor) + ")");

		BSky sky;
		sky.name      = readString(reader);
		sky.sky       = readRoute(reader);
		sky.mipLevel  = reader.readPod<uint32_t>();
		sky.rotationY = reader.readPod<float>();
		return sky;
	}

	void
	saveSky(const BSky& sky, const std::filesystem::path& path)
	{
		const auto bytes = serializeSky(sky);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(fileErrorMessage("bsky: cannot open file for writing", path));

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error(fileErrorMessage("bsky: failed to write file", path));
	}

	BSky
	loadSky(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeSky(bytes);
	}
}
