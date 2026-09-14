#define NOMINMAX

#include <assetlib/bmesh.h>
#include <assetlib/transform.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/Camera.h>
#include <bgl/glm.h>
#include <cstdint>
#include <headless/framing.h>
#include <limits>

namespace headless
{
	assetlib::Bounds
	EmptyBounds() noexcept
	{
		return assetlib::Bounds{ .min = glm::vec3((std::numeric_limits<float>::max)()),
			                     .max = glm::vec3(std::numeric_limits<float>::lowest()) };
	}

	void
	GrowBounds(
		assetlib::Bounds&       bounds,
		const glm::mat4&        transform,
		const assetlib::Bounds& local) noexcept
	{
		for (int corner = 0; corner < 8; ++corner)
		{
			const auto point = glm::vec3(
				(corner & 1) ? local.max.x : local.min.x,
				(corner & 2) ? local.max.y : local.min.y,
				(corner & 4) ? local.max.z : local.min.z);
			const auto placed = glm::vec3(transform * glm::vec4(point, 1.0f));
			bounds.min        = glm::min(bounds.min, placed);
			bounds.max        = glm::max(bounds.max, placed);
		}
	}

	assetlib::Bounds
	MeshEntryBounds(const assetlib::BMesh& mesh, const uint32_t meshIndex)
	{
		auto                  bounds = EmptyBounds();
		const assetlib::Mesh& entry  = mesh.meshes.at(meshIndex);
		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			const assetlib::Submesh& sub = mesh.submeshes.at(entry.firstSubmesh + s);
			GrowBounds(bounds, glm::mat4(1.0f), assetlib::Bounds{ sub.aabbMin, sub.aabbMax });
		}
		return bounds;
	}

	glm::mat4
	InstanceTransform(const assetlib::BMesh& mesh, const uint32_t node)
	{
		if (assetlib::isSkinned(mesh, mesh.nodes.at(node).mesh))
			return glm::mat4(1.0f);

		auto world = glm::mat4(1.0f);
		for (uint32_t n = node; n != assetlib::c_InvalidIndex; n = mesh.nodes[n].parent)
		{
			world = assetlib::toMatrix(mesh.nodes[n].localTransform) * world;
		}
		return world;
	}

	bgl::Camera
	FrameBounds(const assetlib::Bounds& bounds, const uint32_t width, const uint32_t height)
	{
		const glm::vec3 centre = (bounds.min + bounds.max) * 0.5f;
		const float     radius = glm::max(glm::length(bounds.max - centre), 0.001f);
		const float     fovY   = glm::radians(60.0f);
		const float     dist   = radius / glm::tan(fovY * 0.5f) * 1.4f;
		const float     aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				centre + glm::normalize(glm::vec3(0.4f, 0.35f, 1.0f)) * dist,
				centre,
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(fovY, aspect, glm::max(radius * 0.02f, 0.01f), dist + radius * 4.0f);
		return camera;
	}
}
