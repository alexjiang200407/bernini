#include <assetlib/bskel_io.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/skeleton.h>

#include "chunk_io.h"
#include "fs_util.h"

#include <assetlib_structs/magic.h>

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint16_t c_VersionMajor = 1;
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "bskel";

		enum class ChunkId : uint32_t
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
		writer.Add(ChunkId::kStringPool, skeleton.stringPool.bytes());
		return writer.Finish(magic::c_BSkel, c_VersionMajor, c_VersionMinor);
	}

	Skeleton
	deserializeSkeleton(std::span<const std::byte> bytes)
	{
		const chunk::Reader reader(bytes, magic::c_BSkel, c_VersionMajor, c_What);

		Skeleton skeleton;
		skeleton.bones      = reader.Require<Bone>(ChunkId::kBones);
		skeleton.stringPool = core::string_pool(reader.Read<char>(ChunkId::kStringPool));

		validateSkeleton(skeleton);
		return skeleton;
	}

	void
	saveSkeleton(const Skeleton& skeleton, const std::filesystem::path& path)
	{
		writeFileBytes(path, serializeSkeleton(skeleton), "bskel");
	}

	Skeleton
	loadSkeleton(const std::filesystem::path& path)
	{
		return deserializeSkeleton(core::file::read_file_bytes(path.string()));
	}

	Skeleton
	loadSkeleton(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeSkeleton(fileSystem.Read(path));
	}
}
