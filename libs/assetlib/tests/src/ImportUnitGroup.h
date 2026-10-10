#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assetlib::test
{
	/**
	 * Imports `glb` into `dataRoot` as the group `name`, by the same writers the CLI and
	 * the editor call: the copied source and its document in `Authored/Meshes/`, the mesh in
	 * `Derived/Meshes/`, and -- when the source carries a skin -- the rig in `Derived/Skeletons/`
	 * and `Derived/Animations/`, and -- when it carries POINTS -- its grass beside the mesh.
	 * Submesh 0 is bound to `material`, recorded in the document.
	 *
	 * `textureDir` extracts the source's textures into that folder as an import with textures
	 * turned on does, and records it in the document; empty extracts none.
	 */
	inline void
	ImportUnitGroup(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& glb,
		std::string_view             material   = "Authored/Materials/red.bmaterial",
		float                        sampleRate = c_DefaultSampleRate,
		std::string_view             textureDir = {},
		std::string_view             name       = "unit",
		ImportIdentity               identity   = {})
	{
		const auto imported = loadFromGltf(glb, { .sampleRate = sampleRate });

		BMesh mesh = toBMesh(imported);
		generateTangents(mesh);
		requireUniqueSubmeshNames(mesh);

		ImportTarget target{
			std::format("{}/{}{}", c_MeshSourcesDirectoryName, name, c_ImportedSourceExtension),
			sampleRate,
			std::string(textureDir)
		};
		const AssetStore store(dataRoot);
		target.identity        = identity;
		const SourceRef source = store.CopyImportedSource(glb, target);
		mesh.source            = source;

		if (!textureDir.empty())
			target.textures = store.WriteTextures(imported, textureDir);

		auto rig = store.WriteImportedRig(
			imported.skeleton,
			imported.animations,
			mesh,
			identity.id ? importOutputKey(identity, AssetType::kSkeleton) :
						  std::format("Derived/Skeletons/{}.bskel", name),
			identity.id ? importOutputKey(identity, AssetType::kAnimation) :
						  std::format("Derived/Animations/{}.banim", name),
			true,
			source);

		if (!mesh.submeshes.empty())
			target.bindings = std::vector<MaterialBinding>{
				{ std::string(mesh.stringPool.at(mesh.submeshes[0].nameOffset)),
				  std::string(material) }
			};

		const std::string meshKey = identity.id ? importOutputKey(identity, AssetType::kMesh) :
		                                          std::format("Derived/Meshes/{}.bmesh", name);
		store.Save(mesh, meshKey);

		rig.outputs.emplace_back(meshKey);
		target.skeleton = std::move(rig.skeleton);
		target.outputs  = std::move(rig.outputs);

		store.WriteImportedDocument(target, &mesh);
	}
}
