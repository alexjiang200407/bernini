#include <assetlib/container_info.h>

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cache_io.h"

namespace assetlib
{
	std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes)
	{
		if (!cache::isCacheEntry(bytes))
			return std::nullopt;
		uint32_t magic = 0;
		std::memcpy(&magic, bytes.data(), sizeof(magic));

		const cache::PeekedKey key = cache::peekKey(bytes, magic, "cache entry");
		return CacheEntryInfo{ magic, key.bakeToken, key.source };
	}

	bool
	isTextAssetDocument(std::span<const std::byte> bytes) noexcept
	{
		for (const std::byte byte : bytes)
		{
			const char c = static_cast<char>(byte);
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				continue;
			return c == '{';
		}
		return false;
	}

	namespace
	{
		template <typename T>
		uint64_t
		vectorBytes(const std::vector<T>& values) noexcept
		{
			return static_cast<uint64_t>(values.size()) * sizeof(T);
		}

		/** A vector of strings costs its own array plus whatever each string put on the heap. */
		uint64_t
		stringVectorBytes(const std::vector<std::string>& values) noexcept
		{
			uint64_t bytes = vectorBytes(values);
			for (const std::string& value : values) bytes += value.size();
			return bytes;
		}
	}

	uint64_t
	residentBytes(const BMesh& mesh) noexcept
	{
		// Every vector it holds, named rather than summarised, so a field added later is not
		// silently free. The lone strings beside them -- `skeleton`, and the source's mount key --
		// are paths, and no budget turns on a handful of them.
		return vectorBytes(mesh.nodes) + vectorBytes(mesh.roots) + vectorBytes(mesh.meshes) +
		       vectorBytes(mesh.submeshes) + vectorBytes(mesh.meshlets) +
		       vectorBytes(mesh.meshletVertices) + vectorBytes(mesh.meshletTriangles) +
		       vectorBytes(mesh.vertexData) + vectorBytes(mesh.indexData) +
		       vectorBytes(mesh.stringPool.bytes()) + stringVectorBytes(mesh.materials);
	}

	uint64_t
	residentBytes(const Skeleton& skeleton) noexcept
	{
		return vectorBytes(skeleton.bones) + vectorBytes(skeleton.stringPool.bytes());
	}

	uint64_t
	residentBytes(const AnimationSet& animations) noexcept
	{
		// samples is nearly all of it: boneCount * frameCount Transforms, 59.7 MB on the reference
		// rig (docs/skinning.md). The rest is named for the same reason as the meshes' above.
		return vectorBytes(animations.clips) + vectorBytes(animations.samples) +
		       vectorBytes(animations.posedBoxes) + vectorBytes(animations.stringPool.bytes());
	}
}
