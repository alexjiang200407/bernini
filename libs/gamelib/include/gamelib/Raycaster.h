#pragma once
#include <cstdint>
#include <gamelib/Ray.h>
#include <optional>
#include <vector>

namespace assetlib
{
	struct BMesh;
}

namespace game
{
	/**
	 * CPU picking over a set of placed geometries: register the shapes, place instances of them,
	 * then ask which (instance, submesh) a world-space ray meets first.
	 *
	 * It keeps its own compact copy of the geometry (positions and triangles), because nothing else
	 * retains one after the upload to the GPU -- feed it while the source data is still in scope.
	 * Intersection is brute force behind a per-submesh AABB cut, which is plenty for picking at
	 * editor scale; no acceleration structure is built.
	 *
	 * Not thread-safe; the owner serializes access like any other container.
	 */
	class Raycaster
	{
	public:
		struct Hit
		{
			uint32_t instance;      // as returned by AddInstance
			uint32_t submeshIndex;  // within the instance's geometry
			float    t;             // parametric distance along the cast ray
		};

		/**
		 * Registers mesh `meshIndex` of `mesh` as a raycastable geometry. Triangles are read from
		 * the meshlet streams -- the same data the renderer draws, present even when a submesh
		 * carries no index buffer.
		 *
		 * @return The geometry's index, for AddInstance.
		 * @throws std::runtime_error if `meshIndex` is out of range, a submesh has no float3
		 *         position attribute, or a range oversteps its pool.
		 */
		uint32_t
		AddMesh(const assetlib::BMesh& mesh, uint32_t meshIndex);

		/**
		 * Registers an analytic sphere of `radius` about its local origin, reported as submesh 0.
		 * For procedural geometry whose triangles never exist on the CPU.
		 *
		 * @return The geometry's index, for AddInstance.
		 */
		uint32_t
		AddSphere(float radius);

		/**
		 * Places an instance of `geometry` in the world. Instances are compared against every ray
		 * until Clear().
		 *
		 * @param transform Local-to-world; must be invertible.
		 * @return The instance index a Hit reports.
		 * @throws std::runtime_error if `geometry` names nothing registered.
		 */
		uint32_t
		AddInstance(uint32_t geometry, const glm::mat4& transform);

		// Forgets every geometry and instance.
		void
		Clear() noexcept;

		/**
		 * The first thing `ray` meets across every instance: the hit with the smallest `t`, or
		 * nullopt for a clean miss.
		 */
		[[nodiscard]] std::optional<Hit>
		Raycast(const game::Ray& ray) const noexcept;

	private:
		struct Submesh
		{
			std::vector<glm::vec3> positions;
			std::vector<uint32_t>  triangles;  // position indices, 3 per triangle
			glm::vec3              aabbMin;
			glm::vec3              aabbMax;
		};

		struct Geometry
		{
			std::vector<Submesh> submeshes;
			std::optional<float> sphereRadius;  // set: analytic, submeshes stays empty
		};

		struct Instance
		{
			uint32_t  geometry;
			glm::mat4 worldToLocal;
		};

		std::vector<Geometry> m_Geometries;
		std::vector<Instance> m_Instances;
	};
}
