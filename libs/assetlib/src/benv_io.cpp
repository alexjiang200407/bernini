#include "assetlib/benv_io.h"

#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <core/math.h>

#include "fs_util.h"
#include "string_io.h"

namespace assetlib
{
	namespace
	{
		// Bumped whenever the layout changes. loadBenv refuses a major it does not know rather than
		// guessing, because every misread here produces a plausible-looking environment.
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		constexpr size_t c_ChunkAlign = 16;

		enum class ChunkId : uint32_t
		{
			kPrefilter  = 0,
			kIrradiance = 1,
			kSkybox     = 2,
		};

		struct FileHeader
		{
			uint32_t magic;
			uint16_t versionMajor;
			uint16_t versionMinor;
			uint8_t  byteOrder;  // 0 == little-endian
			uint8_t  pad[3];
			uint32_t chunkCount;
			uint32_t chunkTableOffset;
			float    exposure;
			uint64_t sourceHash;
			uint32_t samples;
			uint32_t mipLevels;
			uint64_t fileSize;
		};

		static_assert(sizeof(FileHeader) == 48);

		struct ChunkEntry
		{
			uint32_t id;
			uint32_t pad;
			uint64_t offset;
			uint64_t byteSize;
		};

		static_assert(sizeof(ChunkEntry) == 24);

		void
		padToChunkAlignment(std::vector<std::byte>& out)
		{
			out.resize(core::align(out.size(), c_ChunkAlign), std::byte{ 0 });
		}

		const ImageData&
		require(const ImageData& map, const char* which)
		{
			if (map.subresources.empty() || map.pixels.size() == 0)
				core::throw_runtime_error("assetlib::writeBenv: the {} map is empty", which);
			return map;
		}
	}

	namespace
	{
		// The reference layout. 1 is the blob format above; refusing it by number is what turns a
		// stale file into "re-import" rather than a misread.
		constexpr uint16_t c_EnvVersionMajor = 2;
		constexpr uint16_t c_EnvVersionMinor = 0;
	}

	std::vector<std::byte>
	serializeEnv(const BEnv& env)
	{
		core::io::ByteWriter writer;
		writer.writePod(magic::c_BEnv);
		writer.writePod(c_EnvVersionMajor);
		writer.writePod(c_EnvVersionMinor);

		writeString(writer, env.name);
		writeString(writer, env.sky);
		writeString(writer, env.lighting);

		return writer.take();
	}

	BEnv
	deserializeEnv(std::span<const std::byte> bytes)
	{
		core::io::ByteReader reader(bytes);

		if (reader.readPod<uint32_t>() != magic::c_BEnv)
			throw std::runtime_error("benv: bad magic");

		const auto versionMajor = reader.readPod<uint16_t>();

		// The minor version is additive within a major, and nothing here is optional yet.
		static_cast<void>(reader.readPod<uint16_t>());

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
		const auto bytes = serializeEnv(env);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(fileErrorMessage("benv: cannot open file for writing", path));

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error(fileErrorMessage("benv: failed to write file", path));
	}

	BEnv
	loadEnv(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
		return deserializeEnv(bytes);
	}

	uint64_t
	hashFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			core::throw_runtime_error("assetlib::hashFile: cannot open '{}'", path.string());

		uint64_t                    hash = core::hash_seed();
		std::array<char, 64 * 1024> buffer{};
		while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
		       in.gcount() > 0)
		{
			const auto got = static_cast<size_t>(in.gcount());
			hash           = core::hash_bytes(buffer.data(), got, hash);
		}
		return hash;
	}

	void
	writeBenv(
		const EnvironmentMaps&       maps,
		const std::filesystem::path& path,
		const EnvironmentProvenance& provenance)
	{
		const std::vector<std::byte> blobs[3] = {
			encodeKTX2(require(maps.prefilter, "prefilter")),
			encodeKTX2(require(maps.irradiance, "irradiance")),
			encodeKTX2(require(maps.skybox, "skybox")),
		};
		constexpr ChunkId c_Ids[3] = { ChunkId::kPrefilter,
			                           ChunkId::kIrradiance,
			                           ChunkId::kSkybox };

		auto out = std::vector<std::byte>(sizeof(FileHeader));

		ChunkEntry entries[3] = {};
		for (int i = 0; i < 3; ++i)
		{
			padToChunkAlignment(out);
			entries[i].id       = static_cast<uint32_t>(c_Ids[i]);
			entries[i].offset   = out.size();
			entries[i].byteSize = blobs[i].size();
			out.insert(out.end(), blobs[i].begin(), blobs[i].end());
		}

		padToChunkAlignment(out);
		const size_t tableOffset = out.size();
		out.resize(out.size() + sizeof(entries));
		std::memcpy(out.data() + tableOffset, entries, sizeof(entries));

		FileHeader header{};
		header.magic            = magic::c_BEnv;
		header.versionMajor     = c_VersionMajor;
		header.versionMinor     = c_VersionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = 3;
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.exposure         = maps.exposure;
		header.sourceHash       = provenance.sourceHash;
		header.samples          = provenance.samples;
		header.mipLevels        = provenance.mipLevels;
		header.fileSize         = out.size();
		std::memcpy(out.data(), &header, sizeof(header));

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
			core::throw_runtime_error("assetlib::writeBenv: cannot open '{}'", path.string());

		file.write(
			reinterpret_cast<const char*>(out.data()),
			static_cast<std::streamsize>(out.size()));
		if (!file)
			core::throw_runtime_error("assetlib::writeBenv: failed writing '{}'", path.string());
	}

	namespace
	{
		struct V1File
		{
			FileHeader             header{};
			ChunkEntry             entries[3] = {};
			std::vector<std::byte> bytes;
		};

		/** Reads and validates a v1 file down to its chunk table, leaving the blobs as raw bytes. */
		V1File
		readV1(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary | std::ios::ate);
			if (!in)
				core::throw_runtime_error("assetlib::loadBenv: cannot open '{}'", path.string());

			const auto size = static_cast<size_t>(in.tellg());
			in.seekg(0);

			V1File file;
			file.bytes.resize(size);
			if (size < sizeof(FileHeader) || !in.read(
												 reinterpret_cast<char*>(file.bytes.data()),
												 static_cast<std::streamsize>(size)))
				core::throw_runtime_error("assetlib::loadBenv: '{}' is truncated", path.string());

			std::memcpy(&file.header, file.bytes.data(), sizeof(file.header));

			if (file.header.magic != magic::c_BEnv)
				core::throw_runtime_error(
					"assetlib::loadBenv: '{}' is not a .benv (bad magic)",
					path.string());
			if (file.header.versionMajor != c_VersionMajor)
				core::throw_runtime_error(
					"assetlib::loadBenv: '{}' is version {}, this build reads {}",
					path.string(),
					file.header.versionMajor,
					c_VersionMajor);
			if (file.header.chunkCount != 3 ||
			    file.header.chunkTableOffset + 3 * sizeof(ChunkEntry) > file.bytes.size())
				core::throw_runtime_error(
					"assetlib::loadBenv: '{}' has a malformed chunk table",
					path.string());

			std::memcpy(
				file.entries,
				file.bytes.data() + file.header.chunkTableOffset,
				sizeof(file.entries));

			for (const ChunkEntry& entry : file.entries)
			{
				// Subtract rather than add: both are uint64_t straight out of the file, so a corrupt
				// table could wrap `offset + byteSize` back under the bound while still pointing past
				// the buffer, which a reader would then walk out of range.
				if (entry.offset > file.bytes.size() ||
				    entry.byteSize > file.bytes.size() - entry.offset)
					core::throw_runtime_error(
						"assetlib::loadBenv: a chunk of '{}' runs past the file",
						path.string());
			}

			return file;
		}
	}

	EnvironmentMaps
	loadBenv(const std::filesystem::path& path, EnvironmentProvenance* provenance)
	{
		const V1File      file   = readV1(path);
		const FileHeader& header = file.header;

		EnvironmentMaps maps;
		maps.exposure = header.exposure;

		bool seen[3] = {};
		for (const ChunkEntry& entry : file.entries)
		{
			const auto blob =
				std::span<const std::byte>(file.bytes.data() + entry.offset, entry.byteSize);

			switch (static_cast<ChunkId>(entry.id))
			{
			case ChunkId::kPrefilter:
				maps.prefilter = decodeKTX2(blob);
				seen[0]        = true;
				break;
			case ChunkId::kIrradiance:
				maps.irradiance = decodeKTX2(blob);
				seen[1]         = true;
				break;
			case ChunkId::kSkybox:
				maps.skybox = decodeKTX2(blob);
				seen[2]     = true;
				break;
			default:
				core::throw_runtime_error(
					"assetlib::loadBenv: '{}' has an unknown chunk id {}",
					path.string(),
					entry.id);
			}
		}

		if (!seen[0] || !seen[1] || !seen[2])
			core::throw_runtime_error(
				"assetlib::loadBenv: '{}' is missing one of the three maps",
				path.string());

		if (provenance != nullptr)
		{
			provenance->sourceHash = header.sourceHash;
			provenance->samples    = header.samples;
			provenance->mipLevels  = header.mipLevels;
		}

		return maps;
	}

	std::filesystem::path
	splitBenv(
		const std::filesystem::path& v1Path,
		const std::filesystem::path& outDir,
		const std::string&           name)
	{
		const V1File file = readV1(v1Path);

		createDirectories(outDir);

		const auto writeBlob = [&](const ChunkEntry& entry, const std::string& fileName) {
			errno = 0;
			std::ofstream out(outDir / fileName, std::ios::binary);
			if (!out)
				throw std::runtime_error(
					fileErrorMessage("splitBenv: cannot open file for writing", outDir / fileName));
			out.write(
				reinterpret_cast<const char*>(file.bytes.data() + entry.offset),
				static_cast<std::streamsize>(entry.byteSize));
			if (!out)
				throw std::runtime_error(
					fileErrorMessage("splitBenv: failed to write file", outDir / fileName));
		};

		std::string mapNames[3];
		for (const ChunkEntry& entry : file.entries)
		{
			switch (static_cast<ChunkId>(entry.id))
			{
			case ChunkId::kPrefilter:
				mapNames[0] = name + "_prefilter.ktx2";
				writeBlob(entry, mapNames[0]);
				break;
			case ChunkId::kIrradiance:
				mapNames[1] = name + "_irradiance.ktx2";
				writeBlob(entry, mapNames[1]);
				break;
			case ChunkId::kSkybox:
				mapNames[2] = name + "_sky.ktx2";
				writeBlob(entry, mapNames[2]);
				break;
			default:
				core::throw_runtime_error(
					"assetlib::splitBenv: '{}' has an unknown chunk id {}",
					v1Path.string(),
					entry.id);
			}
		}

		BSky sky;
		sky.name      = name;
		sky.sky.baked = mapNames[2];
		saveSky(sky, outDir / (name + ".bsky"));

		BEnvLighting lighting;
		lighting.name             = name;
		lighting.prefilter.baked  = mapNames[0];
		lighting.irradiance.baked = mapNames[1];
		lighting.exposure         = file.header.exposure;
		saveEnvLighting(lighting, outDir / (name + ".benvl"));

		BEnv env;
		env.name     = name;
		env.sky      = name + ".bsky";
		env.lighting = name + ".benvl";

		const std::filesystem::path envPath = outDir / (name + ".benv");
		saveEnv(env, envPath);
		return envPath;
	}
}
