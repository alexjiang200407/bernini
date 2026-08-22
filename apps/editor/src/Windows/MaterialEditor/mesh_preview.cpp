#include "Windows/MaterialEditor/mesh_preview.h"

#include "Mesh/BMeshUtil.h"

#include <assetlib/mesh_tangents.h>

namespace editor
{
	namespace
	{
		QString
		ResolveMaterialPath(
			const assetlib::BMesh&       mesh,
			const assetlib::Submesh&     submesh,
			const std::filesystem::path& dataRoot)
		{
			if (dataRoot.empty() || submesh.material >= mesh.materials.size())
				return {};

			const std::string& relative = mesh.materials[submesh.material];
			if (relative.empty())
				return {};

			return QString::fromStdWString((dataRoot / relative).lexically_normal().wstring());
		}
	}

	MeshPreviewBuild
	PrepareMeshPreview(const assetlib::BMesh& mesh, const std::filesystem::path& dataRoot)
	{
		auto out = MeshPreviewBuild();

		// A .bmesh spreads its submeshes across several meshes, and a node instances a mesh (the
		// same mesh can be instanced by several nodes). Cook each mesh once, then place an instance
		// for every node that references one, at that node's world transform.
		auto entryForMesh = std::unordered_map<uint32_t, uint32_t>();

		for (uint32_t nodeIndex = 0; nodeIndex < mesh.nodes.size(); ++nodeIndex)
		{
			const assetlib::Node& node = mesh.nodes[nodeIndex];
			if (!bmesh::ReferencesMesh(mesh, node))
				continue;

			auto [it, inserted] =
				entryForMesh.try_emplace(node.mesh, static_cast<uint32_t>(out.meshes.size()));
			if (inserted)
			{
				auto entry            = PreparedPreviewMesh();
				entry.meshIndex       = node.mesh;
				entry.cooked          = bgl::CookStaticMesh(mesh, node.mesh);
				entry.raycastGeometry = out.raycaster.AddMesh(mesh, node.mesh);
				out.meshes.push_back(std::move(entry));

				// Name each of this mesh's submeshes once, in the order the selector shows them.
				const assetlib::Mesh& meshEntry = mesh.meshes[node.mesh];
				out.submeshRefs.reserve(out.submeshRefs.size() + meshEntry.submeshCount);

				for (uint32_t i = 0; i < meshEntry.submeshCount; ++i)
				{
					const assetlib::Submesh& submesh = mesh.submeshes[meshEntry.firstSubmesh + i];

					const std::string_view pooled = mesh.stringPool.at(submesh.nameOffset);
					auto                   label =
						QString::fromUtf8(pooled.data(), static_cast<qsizetype>(pooled.size()));
					if (label.isEmpty())
						label = QString("Submesh %1").arg(out.submeshNames.size());

					out.submeshNames << label;
					out.submeshMaterialPaths << ResolveMaterialPath(mesh, submesh, dataRoot);
					out.submeshRefs.push_back(
						{ it->second,
					      i,
					      meshEntry.firstSubmesh + i,
					      assetlib::hasTangent(submesh) });
				}
			}

			const glm::mat4 world = bmesh::GetInstanceTransform(mesh, nodeIndex);
			out.placements.push_back({ it->second, world });
			out.raycaster.AddInstance(out.meshes[it->second].raycastGeometry, world);
		}

		if (out.meshes.empty())
			throw std::runtime_error("no node references a mesh");

		return out;
	}
}
