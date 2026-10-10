#include "bmesh/submesh_read.h"

#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assetlib
{
	std::vector<uint32_t>
	readSubmeshIndices(const std::span<const std::byte> indexData, const Submesh& submesh)
	{
		auto indices = std::vector<uint32_t>();
		if (submesh.indexType == IndexType::kNone || submesh.indexCount == 0)
			return indices;

		const size_t width = submesh.indexType == IndexType::kUint16 ? 2 : 4;
		const size_t end =
			static_cast<size_t>(submesh.indexByteOffset) + submesh.indexCount * width;
		if (end > indexData.size())
			core::throw_runtime_error("bmesh: a submesh's index range runs past the pool");

		indices.reserve(submesh.indexCount);
		const std::byte* base = indexData.data() + submesh.indexByteOffset;
		if (submesh.indexType == IndexType::kUint16)
		{
			const auto* src = reinterpret_cast<const uint16_t*>(base);
			for (uint32_t i = 0; i < submesh.indexCount; ++i) indices.push_back(src[i]);
		}
		else
		{
			const auto* src = reinterpret_cast<const uint32_t*>(base);
			for (uint32_t i = 0; i < submesh.indexCount; ++i) indices.push_back(src[i]);
		}
		return indices;
	}

	const float*
	floatsAt(
		const std::span<const std::byte> vertexData,
		const Submesh&                   submesh,
		const VertexAttribute&           attribute,
		const uint32_t                   vertex) noexcept
	{
		const size_t offset = static_cast<size_t>(submesh.vertexByteOffset) +
		                      static_cast<size_t>(vertex) * submesh.layout.stride +
		                      attribute.offset;
		return reinterpret_cast<const float*>(vertexData.data() + offset);
	}
}
