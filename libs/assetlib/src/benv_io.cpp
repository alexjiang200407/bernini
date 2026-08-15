#include "assetlib/benv_io.h"

#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>
#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

#include "fs_util.h"
#include "string_io.h"

namespace assetlib
{
	namespace
	{
		// 1 was the retired blob format that embedded the three maps as KTX2 chunks. Refusing it by
		// number is what turns a stale file into "re-import" rather than a misread: its version field
		// sits at the same offset, and what follows would be read as string lengths.
		constexpr uint16_t c_EnvVersionMajor = 2;
		constexpr uint16_t c_EnvVersionMinor = 0;
	}

	std::vector<std::byte>
	serializeEnv(const BEnv& env)
	{
		core::io::ByteWriter writer;
		writer.WritePod(magic::c_BEnv);
		writer.WritePod(c_EnvVersionMajor);
		writer.WritePod(c_EnvVersionMinor);

		writeString(writer, env.name);
		writeString(writer, env.sky);
		writeString(writer, env.lighting);

		return writer.Take();
	}

	BEnv
	deserializeEnv(std::span<const std::byte> bytes)
	{
		core::io::ByteReader reader(bytes);

		if (reader.ReadPod<uint32_t>() != magic::c_BEnv)
			throw std::runtime_error("benv: bad magic");

		const auto versionMajor = reader.ReadPod<uint16_t>();

		// The minor version is additive within a major, and nothing here is optional yet.
		static_cast<void>(reader.ReadPod<uint16_t>());

		if (versionMajor != c_EnvVersionMajor)
			throw std::runtime_error(
				"benv: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_EnvVersionMajor) +
				"); a v1 .benv holds baked maps and must be re-imported as .bsky + .benvl");

		BEnv env;
		env.name     = readString(reader);
		env.sky      = readString(reader);
		env.lighting = readString(reader);
		return env;
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
