#include <assetlib/mesh_tangents.h>

#include <assetlib_structs/BMesh.h>

#include <glm/glm.hpp>

namespace assetlib
{
	namespace
	{
		const VertexAttribute*
		findAttribute(const VertexLayout& layout, VertexSemantic semantic) noexcept
		{
			for (uint8_t i = 0; i < layout.attributeCount; ++i)
			{
				if (layout.attributes[i].semantic == semantic)
					return &layout.attributes[i];
			}
			return nullptr;
		}

		/** A float attribute read straight out of the interleaved blob. */
		const float*
		floatsAt(
			const std::vector<std::byte>& pool,
			uint32_t                      vertexByteOffset,
			const VertexLayout&           layout,
			const VertexAttribute&        attribute,
			uint32_t                      vertex) noexcept
		{
			const size_t offset = static_cast<size_t>(vertexByteOffset) +
			                      static_cast<size_t>(vertex) * layout.stride + attribute.offset;

			return reinterpret_cast<const float*>(pool.data() + offset);
		}

		/**
		 * The submesh's triangles, as vertex indices local to it. Empty when it carries no index
		 * buffer -- there is no triangle list to accumulate a UV basis over.
		 */
		std::vector<uint32_t>
		readIndices(const BMesh& mesh, const Submesh& submesh)
		{
			auto indices = std::vector<uint32_t>();
			if (submesh.indexType == IndexType::kNone || submesh.indexCount == 0)
				return indices;

			indices.reserve(submesh.indexCount);
			const std::byte* base = mesh.indexData.data() + submesh.indexByteOffset;

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

		/**
		 * Any unit vector perpendicular to `n`. For a vertex whose triangles cancelled out, which has
		 * no meaningful tangent to compute -- the frame is arbitrary but at least well-formed.
		 */
		glm::vec3
		anyPerpendicular(const glm::vec3& n) noexcept
		{
			const glm::vec3 axis =
				std::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

			return glm::normalize(glm::cross(axis, n));
		}

		/** The per-vertex tangent basis for one submesh, accumulated over its triangles. */
		std::vector<glm::vec4>
		deriveTangents(
			const BMesh&                 mesh,
			const Submesh&               submesh,
			const VertexAttribute&       position,
			const VertexAttribute&       normal,
			const VertexAttribute&       uv,
			const std::vector<uint32_t>& indices)
		{
			const VertexLayout& layout = submesh.layout;

			auto tangents   = std::vector<glm::vec3>(submesh.vertexCount, glm::vec3(0.0f));
			auto bitangents = std::vector<glm::vec3>(submesh.vertexCount, glm::vec3(0.0f));

			const auto positionOf = [&](uint32_t v) {
				const float* p =
					floatsAt(mesh.vertexData, submesh.vertexByteOffset, layout, position, v);
				return glm::vec3(p[0], p[1], p[2]);
			};
			const auto uvOf = [&](uint32_t v) {
				const float* t = floatsAt(mesh.vertexData, submesh.vertexByteOffset, layout, uv, v);
				return glm::vec2(t[0], t[1]);
			};

			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const uint32_t i0 = indices[i];
				const uint32_t i1 = indices[i + 1];
				const uint32_t i2 = indices[i + 2];

				if (i0 >= submesh.vertexCount || i1 >= submesh.vertexCount ||
				    i2 >= submesh.vertexCount)
				{
					throw std::runtime_error(
						"assetlib::generateTangents: an index points outside its submesh");
				}

				const glm::vec3 e1 = positionOf(i1) - positionOf(i0);
				const glm::vec3 e2 = positionOf(i2) - positionOf(i0);
				const glm::vec2 d1 = uvOf(i1) - uvOf(i0);
				const glm::vec2 d2 = uvOf(i2) - uvOf(i0);

				// A triangle with no UV area gives no direction to derive from; its neighbours cover
				// the vertices it shares.
				const float area = d1.x * d2.y - d2.x * d1.y;
				if (std::abs(area) < 1e-12f)
					continue;

				const float     r = 1.0f / area;
				const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
				const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

				for (const uint32_t v : { i0, i1, i2 })
				{
					tangents[v] += t;
					bitangents[v] += b;
				}
			}

			auto basis = std::vector<glm::vec4>(submesh.vertexCount);
			for (uint32_t v = 0; v < submesh.vertexCount; ++v)
			{
				const float* n =
					floatsAt(mesh.vertexData, submesh.vertexByteOffset, layout, normal, v);
				const glm::vec3 vn = glm::normalize(glm::vec3(n[0], n[1], n[2]));

				glm::vec3 t = tangents[v] - vn * glm::dot(vn, tangents[v]);

				t = glm::dot(t, t) > 1e-16f ? glm::normalize(t) : anyPerpendicular(vn);

				// Matches the shader, which derives the bitangent as cross(N, T) * w.
				const float w = glm::dot(glm::cross(vn, t), bitangents[v]) < 0.0f ? -1.0f : 1.0f;

				basis[v] = glm::vec4(t, w);
			}

			return basis;
		}

		void
		appendVertex(
			std::vector<std::byte>&       out,
			const std::vector<std::byte>& pool,
			uint32_t                      vertexByteOffset,
			const VertexLayout&           layout,
			uint32_t                      vertex)
		{
			const std::byte* src =
				pool.data() + vertexByteOffset + static_cast<size_t>(vertex) * layout.stride;

			out.insert(out.end(), src, src + layout.stride);
		}
	}

	bool
	hasTangent(const Submesh& submesh) noexcept
	{
		return findAttribute(submesh.layout, VertexSemantic::kTangent) != nullptr;
	}

	TangentGenResult
	generateTangents(BMesh& mesh)
	{
		auto result = TangentGenResult();

		// What each submesh will carry, decided before anything is written: a submesh that gains an
		// attribute grows its stride, which moves every submesh after it in the shared pool.
		auto derived = std::vector<std::vector<glm::vec4>>(mesh.submeshes.size());

		for (size_t s = 0; s < mesh.submeshes.size(); ++s)
		{
			const Submesh& submesh = mesh.submeshes[s];

			if (hasTangent(submesh))
			{
				++result.kept;
				continue;
			}

			const VertexAttribute* position =
				findAttribute(submesh.layout, VertexSemantic::kPosition);
			const VertexAttribute* normal = findAttribute(submesh.layout, VertexSemantic::kNormal);
			const VertexAttribute* uv = findAttribute(submesh.layout, VertexSemantic::kTexCoord0);

			// A tangent frame is a UV-space derivative resolved against the normal, so both are
			// required. Neither is fabricated here: a mesh with no UVs has no normal map to sample.
			const bool derivable = position != nullptr && normal != nullptr && uv != nullptr &&
			                       position->format == VertexFormat::kFloat32x3 &&
			                       normal->format == VertexFormat::kFloat32x3 &&
			                       uv->format == VertexFormat::kFloat32x2;

			if (!derivable || submesh.layout.attributeCount >= VertexLayout::c_MaxAttributes)
			{
				++result.skipped;
				continue;
			}

			const std::vector<uint32_t> indices = readIndices(mesh, submesh);
			if (indices.size() < 3)
			{
				++result.skipped;
				continue;
			}

			const size_t end = static_cast<size_t>(submesh.vertexByteOffset) +
			                   static_cast<size_t>(submesh.vertexCount) * submesh.layout.stride;
			if (end > mesh.vertexData.size())
			{
				throw std::runtime_error(
					"assetlib::generateTangents: a submesh's vertex blob runs past the pool");
			}

			derived[s] = deriveTangents(mesh, submesh, *position, *normal, *uv, indices);
			++result.generated;
		}

		if (result.generated == 0)
			return result;

		// One pass over the pool, rebuilding it submesh by submesh so the offsets can be rewritten as
		// they are laid down.
		auto rebuilt = std::vector<std::byte>();
		rebuilt.reserve(mesh.vertexData.size() + result.generated * 16);

		for (size_t s = 0; s < mesh.submeshes.size(); ++s)
		{
			Submesh&           submesh = mesh.submeshes[s];
			const uint32_t     from    = submesh.vertexByteOffset;
			const VertexLayout old     = submesh.layout;

			submesh.vertexByteOffset = static_cast<uint32_t>(rebuilt.size());

			if (derived[s].empty())
			{
				for (uint32_t v = 0; v < submesh.vertexCount; ++v)
					appendVertex(rebuilt, mesh.vertexData, from, old, v);
				continue;
			}

			// The tangent goes last, so every existing attribute keeps its offset and only the stride
			// changes -- the blob is copied through untouched with four floats appended.
			submesh.layout.attributes[submesh.layout.attributeCount++] = { VertexSemantic::kTangent,
				                                                           VertexFormat::kFloat32x4,
				                                                           old.stride };
			submesh.layout.stride =
				static_cast<uint16_t>(old.stride + formatSize(VertexFormat::kFloat32x4));

			for (uint32_t v = 0; v < submesh.vertexCount; ++v)
			{
				appendVertex(rebuilt, mesh.vertexData, from, old, v);

				const auto* bytes = reinterpret_cast<const std::byte*>(&derived[s][v]);
				rebuilt.insert(rebuilt.end(), bytes, bytes + sizeof(glm::vec4));
			}
		}

		mesh.vertexData = std::move(rebuilt);
		return result;
	}
}
