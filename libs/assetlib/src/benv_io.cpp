#include "assetlib/benv_io.h"

#include <assetlib/image_io.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x564E4542u;  // 'B','E','N','V' little-endian

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
		Pad(std::vector<std::byte>& out)
		{
			while (out.size() % c_ChunkAlign != 0) out.push_back(std::byte{ 0 });
		}

		const ImageData&
		Require(const ImageData& map, const char* which)
		{
			if (map.subresources.empty() || map.pixels.size() == 0)
				throw std::runtime_error(
					std::string("assetlib::writeBenv: the ") + which + " map is empty");
			return map;
		}
	}

	uint64_t
	HashFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("assetlib::HashFile: cannot open '" + path.string() + "'");

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
		const EnvMapSet&             set,
		const std::filesystem::path& path,
		const EnvMapProvenance&      provenance)
	{
		const std::vector<std::byte> blobs[3] = {
			EncodeKTX2(Require(set.prefilter, "prefilter")),
			EncodeKTX2(Require(set.irradiance, "irradiance")),
			EncodeKTX2(Require(set.skybox, "skybox")),
		};
		constexpr ChunkId ids[3] = { ChunkId::kPrefilter, ChunkId::kIrradiance, ChunkId::kSkybox };

		auto out = std::vector<std::byte>(sizeof(FileHeader));

		ChunkEntry entries[3] = {};
		for (int i = 0; i < 3; ++i)
		{
			Pad(out);
			entries[i].id       = static_cast<uint32_t>(ids[i]);
			entries[i].offset   = out.size();
			entries[i].byteSize = blobs[i].size();
			out.insert(out.end(), blobs[i].begin(), blobs[i].end());
		}

		Pad(out);
		const size_t tableOffset = out.size();
		out.resize(out.size() + sizeof(entries));
		std::memcpy(out.data() + tableOffset, entries, sizeof(entries));

		FileHeader header{};
		header.magic            = c_Magic;
		header.versionMajor     = c_VersionMajor;
		header.versionMinor     = c_VersionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = 3;
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.exposure         = set.exposure;
		header.sourceHash       = provenance.sourceHash;
		header.samples          = provenance.samples;
		header.mipLevels        = provenance.mipLevels;
		header.fileSize         = out.size();
		std::memcpy(out.data(), &header, sizeof(header));

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
			throw std::runtime_error("assetlib::writeBenv: cannot open '" + path.string() + "'");

		file.write(
			reinterpret_cast<const char*>(out.data()),
			static_cast<std::streamsize>(out.size()));
		if (!file)
			throw std::runtime_error("assetlib::writeBenv: failed writing '" + path.string() + "'");
	}

	EnvMapSet
	loadBenv(const std::filesystem::path& path, EnvMapProvenance* provenance)
	{
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in)
			throw std::runtime_error("assetlib::loadBenv: cannot open '" + path.string() + "'");

		const auto size = static_cast<size_t>(in.tellg());
		in.seekg(0);

		auto bytes = std::vector<std::byte>(size);
		if (size < sizeof(FileHeader) ||
		    !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
			throw std::runtime_error("assetlib::loadBenv: '" + path.string() + "' is truncated");

		FileHeader header{};
		std::memcpy(&header, bytes.data(), sizeof(header));

		if (header.magic != c_Magic)
			throw std::runtime_error(
				"assetlib::loadBenv: '" + path.string() + "' is not a .benv (bad magic)");
		if (header.versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"assetlib::loadBenv: '" + path.string() + "' is version " +
				std::to_string(header.versionMajor) + ", this build reads " +
				std::to_string(c_VersionMajor));
		if (header.chunkCount != 3 ||
		    header.chunkTableOffset + 3 * sizeof(ChunkEntry) > bytes.size())
			throw std::runtime_error(
				"assetlib::loadBenv: '" + path.string() + "' has a malformed chunk table");

		ChunkEntry entries[3] = {};
		std::memcpy(entries, bytes.data() + header.chunkTableOffset, sizeof(entries));

		EnvMapSet set;
		set.exposure = header.exposure;

		bool seen[3] = {};
		for (const ChunkEntry& entry : entries)
		{
			if (entry.offset + entry.byteSize > bytes.size())
				throw std::runtime_error(
					"assetlib::loadBenv: a chunk of '" + path.string() + "' runs past the file");

			const auto blob =
				std::span<const std::byte>(bytes.data() + entry.offset, entry.byteSize);

			switch (static_cast<ChunkId>(entry.id))
			{
			case ChunkId::kPrefilter:
				set.prefilter = DecodeKTX2(blob);
				seen[0]       = true;
				break;
			case ChunkId::kIrradiance:
				set.irradiance = DecodeKTX2(blob);
				seen[1]        = true;
				break;
			case ChunkId::kSkybox:
				set.skybox = DecodeKTX2(blob);
				seen[2]    = true;
				break;
			default:
				throw std::runtime_error(
					"assetlib::loadBenv: '" + path.string() + "' has an unknown chunk id " +
					std::to_string(entry.id));
			}
		}

		if (!seen[0] || !seen[1] || !seen[2])
			throw std::runtime_error(
				"assetlib::loadBenv: '" + path.string() + "' is missing one of the three maps");

		if (provenance != nullptr)
		{
			provenance->sourceHash = header.sourceHash;
			provenance->samples    = header.samples;
			provenance->mipLevels  = header.mipLevels;
		}

		return set;
	}
}
