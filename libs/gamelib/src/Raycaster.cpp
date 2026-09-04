#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gamelib/Ray.h>
#include <gamelib/Raycaster.h>

#include <assetlib_structs/BMesh.h>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace game
{
	namespace
	{
		std::vector<glm::vec3>
		ReadPositions(const assetlib::BMesh& mesh, const assetlib::Submesh& submesh)
		{
			const assetlib::VertexAttribute* position =
				assetlib::findAttribute(submesh.layout, assetlib::VertexSemantic::kPosition);
			if (position == nullptr || position->format != assetlib::VertexFormat::kFloat32x3)
				throw std::runtime_error("Raycaster: a submesh has no float3 position attribute");

			if (position->offset + sizeof(glm::vec3) > submesh.layout.stride)
				throw std::runtime_error("Raycaster: a position attribute overruns its stride");

			const size_t end = static_cast<size_t>(submesh.vertexByteOffset) +
			                   static_cast<size_t>(submesh.vertexCount) * submesh.layout.stride;
			if (end > mesh.vertexData.size())
				throw std::runtime_error("Raycaster: a submesh's vertex blob runs past the pool");

			auto positions = std::vector<glm::vec3>(submesh.vertexCount);
			for (uint32_t v = 0; v < submesh.vertexCount; ++v)
			{
				std::memcpy(
					&positions[v],
					mesh.vertexData.data() + submesh.vertexByteOffset +
						static_cast<size_t>(v) * submesh.layout.stride + position->offset,
					sizeof(glm::vec3));
			}
			return positions;
		}

		std::vector<uint32_t>
		ReadTriangles(const assetlib::BMesh& mesh, const assetlib::Submesh& submesh)
		{
			auto triangles = std::vector<uint32_t>();

			for (uint32_t m = 0; m < submesh.meshletCount; ++m)
			{
				const size_t index = static_cast<size_t>(submesh.firstMeshlet) + m;
				if (index >= mesh.meshlets.size())
					throw std::runtime_error(
						"Raycaster: a submesh's meshlet range runs past the pool");

				const assetlib::Meshlet& meshlet = mesh.meshlets[index];
				const uint64_t           indexEnd =
					static_cast<uint64_t>(meshlet.triangleOffset) + meshlet.triangleCount * 3ull;
				if (static_cast<uint64_t>(meshlet.vertexOffset) + meshlet.vertexCount >
				        mesh.meshletVertices.size() ||
				    indexEnd > mesh.meshletTriangles.size())
					throw std::runtime_error("Raycaster: a meshlet overflows its streams");

				triangles.reserve(triangles.size() + meshlet.triangleCount * 3u);
				for (uint32_t i = 0; i < meshlet.triangleCount * 3u; ++i)
				{
					const uint8_t local = mesh.meshletTriangles[meshlet.triangleOffset + i];
					if (local >= meshlet.vertexCount)
						throw std::runtime_error(
							"Raycaster: a meshlet index points outside itself");

					const uint32_t vertex = mesh.meshletVertices[meshlet.vertexOffset + local];
					if (vertex >= submesh.vertexCount)
						throw std::runtime_error(
							"Raycaster: a meshlet vertex points outside its submesh");

					triangles.push_back(vertex);
				}
			}

			return triangles;
		}
	}

	uint32_t
	Raycaster::AddMesh(const assetlib::BMesh& mesh, uint32_t meshIndex)
	{
		if (meshIndex >= mesh.meshes.size())
			throw std::runtime_error("Raycaster::AddMesh: mesh index out of range");

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];
		if (static_cast<uint64_t>(entry.firstSubmesh) + entry.submeshCount > mesh.submeshes.size())
			throw std::runtime_error(
				"Raycaster::AddMesh: a mesh's submesh range runs past the pool");

		auto geometry = Geometry();
		geometry.submeshes.reserve(entry.submeshCount);

		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			const assetlib::Submesh& src = mesh.submeshes[entry.firstSubmesh + s];
			geometry.submeshes.push_back(
				{ ReadPositions(mesh, src), ReadTriangles(mesh, src), src.aabbMin, src.aabbMax });
		}

		m_Geometries.push_back(std::move(geometry));
		return static_cast<uint32_t>(m_Geometries.size() - 1);
	}

	uint32_t
	Raycaster::AddSphere(float radius)
	{
		auto geometry         = Geometry();
		geometry.sphereRadius = radius;
		m_Geometries.push_back(std::move(geometry));
		return static_cast<uint32_t>(m_Geometries.size() - 1);
	}

	uint32_t
	Raycaster::AddInstance(uint32_t geometry, const glm::mat4& transform)
	{
		if (geometry >= m_Geometries.size())
			throw std::runtime_error("Raycaster::AddInstance: geometry index out of range");

		m_Instances.push_back({ geometry, glm::inverse(transform) });
		return static_cast<uint32_t>(m_Instances.size() - 1);
	}

	void
	Raycaster::Clear() noexcept
	{
		m_Geometries.clear();
		m_Instances.clear();
	}

	std::optional<Raycaster::Hit>
	Raycaster::Raycast(const game::Ray& ray) const noexcept
	{
		auto best = std::optional<Hit>();

		for (uint32_t i = 0; i < m_Instances.size(); ++i)
		{
			const Instance& instance = m_Instances[i];
			const game::Ray local    = game::Transformed(ray, instance.worldToLocal);
			const Geometry& geometry = m_Geometries[instance.geometry];

			// t survives the transform unscaled (see game::Transformed), so hits from different
			// instances compare directly.
			if (geometry.sphereRadius.has_value())
			{
				const auto t =
					game::IntersectSphere(local, glm::vec3(0.0f), *geometry.sphereRadius);
				if (t.has_value() && (!best.has_value() || *t < best->t))
					best = Hit{ i, 0, *t };
				continue;
			}

			for (uint32_t s = 0; s < geometry.submeshes.size(); ++s)
			{
				const Submesh& submesh = geometry.submeshes[s];

				const auto entry = game::IntersectAabb(local, submesh.aabbMin, submesh.aabbMax);
				if (!entry.has_value() || (best.has_value() && *entry >= best->t))
					continue;

				for (size_t tri = 0; tri + 2 < submesh.triangles.size(); tri += 3)
				{
					const auto t = game::IntersectTriangle(
						local,
						submesh.positions[submesh.triangles[tri]],
						submesh.positions[submesh.triangles[tri + 1]],
						submesh.positions[submesh.triangles[tri + 2]]);
					if (t.has_value() && (!best.has_value() || *t < best->t))
						best = Hit{ i, s, *t };
				}
			}
		}

		return best;
	}
}
