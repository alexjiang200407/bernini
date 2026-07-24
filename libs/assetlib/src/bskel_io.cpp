#include <assetlib/bskel_io.h>

#include <assetlib/skeleton.h>

#include "chunk_io.h"
#include "fs_util.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x4C4B5342u;  // 'B','S','K','L' little-endian

		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "bskel";

		enum ChunkId : uint32_t
		{
			kBones = 1,
			kStringPool
		};
	}

	std::vector<std::byte>
	serializeSkeleton(const Skeleton& skeleton)
	{
		chunk::Writer writer;
		writer.Add(ChunkId::kBones, skeleton.bones);
		writer.Add(ChunkId::kStringPool, skeleton.stringPool);
		return writer.Finish(c_Magic, c_VersionMajor, c_VersionMinor);
	}

	Skeleton
	deserializeSkeleton(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, c_Magic, c_VersionMajor, c_What);

		Skeleton skeleton;
		skeleton.bones      = reader.Require<Bone>(ChunkId::kBones);
		skeleton.stringPool = reader.Read<char>(ChunkId::kStringPool);

		validateSkeleton(skeleton);
		return skeleton;
	}

	void
	saveSkeleton(const Skeleton& skeleton, const std::filesystem::path& path)
	{
		const auto bytes = serializeSkeleton(skeleton);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(fileErrorMessage("bskel: cannot open file for writing", path));

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error(fileErrorMessage("bskel: failed to write file", path));
	}

	Skeleton
	loadSkeleton(const std::filesystem::path& path)
	{
		return deserializeSkeleton(core::file::readFileBytes(path.string()));
	}
}
