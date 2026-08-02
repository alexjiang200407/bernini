#include "assetlib/benv_io.h"

#include "ChunkFile.h"
#include "fs_util.h"

#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>

namespace assetlib
{
	namespace
	{
		// Bumped whenever the layout changes. loadBenv refuses a major it does not know rather than
		// guessing, because every misread here produces a plausible-looking environment.
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		enum class ChunkId : uint32_t
		{
			kPrefilter  = 0,
			kIrradiance = 1,
			kSkybox     = 2,
		};

		struct FileHeader
		{
			ChunkHeader chunks;
			float       exposure;
			uint64_t    sourceHash;
			uint32_t    samples;
			uint32_t    mipLevels;
			uint64_t    fileSize;
		};

		static_assert(sizeof(FileHeader) == 48);

		const ImageData&
		require(const ImageData& map, const char* which)
		{
			if (map.subresources.empty() || map.pixels.size() == 0)
				core::throw_runtime_error("assetlib::writeBenv: the {} map is empty", which);
			return map;
		}
	}

	uint64_t
	hashFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			core::throw_runtime_error("assetlib::hashFile: cannot open '{}'", path.string());

		uint64_t                    hash = 0xcbf29ce484222325ull;
		std::array<char, 64 * 1024> buffer{};
		while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
		       in.gcount() > 0)
		{
			const auto got = static_cast<size_t>(in.gcount());
			for (size_t i = 0; i < got; ++i)
			{
				hash ^= static_cast<uint8_t>(buffer[i]);
				hash *= 0x100000001b3ull;
			}
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

		auto writer = core::io::ByteWriter();
		writer.writePod(FileHeader{});  // placeholder, patched below

		ChunkEntry entries[3] = {};
		for (int i = 0; i < 3; ++i)
			entries[i] = appendBlob(writer, c_Ids[i], std::span<const std::byte>(blobs[i]));

		writer.alignTo(c_ChunkAlign);
		const auto tableOffset = writer.size();
		writer.writePodArray(std::span<const ChunkEntry>(entries));

		FileHeader header{};
		header.chunks.magic            = magic::c_BEnv;
		header.chunks.versionMajor     = c_VersionMajor;
		header.chunks.versionMinor     = c_VersionMinor;
		header.chunks.byteOrder        = 0;
		header.chunks.chunkCount       = 3;
		header.chunks.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.exposure                = maps.exposure;
		header.sourceHash              = provenance.sourceHash;
		header.samples                 = provenance.samples;
		header.mipLevels               = provenance.mipLevels;
		header.fileSize                = writer.size();
		writer.patchPod(0, header);

		writeFileBytes(path, writer.take(), "assetlib::writeBenv");
	}

	EnvironmentMaps
	loadBenv(const std::filesystem::path& path, EnvironmentProvenance* provenance)
	{
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in)
			core::throw_runtime_error("assetlib::loadBenv: cannot open '{}'", path.string());

		const auto size = static_cast<size_t>(in.tellg());
		in.seekg(0);

		auto bytes = std::vector<std::byte>(size);
		if (size < sizeof(FileHeader) ||
		    !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
			core::throw_runtime_error("assetlib::loadBenv: '{}' is truncated", path.string());

		FileHeader header{};
		std::memcpy(&header, bytes.data(), sizeof(header));

		const auto context = std::format("assetlib::loadBenv: '{}'", path.string());
		validateHeader(header.chunks, magic::c_BEnv, c_VersionMajor, context);

		const auto table = ChunkTable(bytes, header.chunks, context);

		EnvironmentMaps maps;
		maps.exposure   = header.exposure;
		maps.prefilter  = decodeKTX2(table.Blob(ChunkId::kPrefilter));
		maps.irradiance = decodeKTX2(table.Blob(ChunkId::kIrradiance));
		maps.skybox     = decodeKTX2(table.Blob(ChunkId::kSkybox));

		if (provenance != nullptr)
		{
			provenance->sourceHash = header.sourceHash;
			provenance->samples    = header.samples;
			provenance->mipLevels  = header.mipLevels;
		}

		return maps;
	}
}
