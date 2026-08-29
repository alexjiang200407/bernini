#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMesh.h>

namespace assetlib::test
{
	/**
	 * Imports `glb` into `dataRoot` as the group `name`, by the same writers the CLI and
	 * the editor call: the copied source and its document in `Authored/Meshes/`, the mesh in
	 * `Derived/Meshes/`, and -- when the source carries a skin -- the rig in `Derived/Skeletons/`
	 * and `Derived/Animations/`. Submesh 0 is bound to `material`, recorded in the document.
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
		std::string_view             name       = "unit")
	{
		const auto imported = loadFromGltf(glb, { .sampleRate = sampleRate });

		BMesh mesh = toBMesh(imported);
		generateTangents(mesh);
		requireUniqueSubmeshNames(mesh);

		ImportTarget     target{ std::string(name), sampleRate, std::string(textureDir) };
		const AssetStore store(dataRoot);
		const SourceRef  source = store.CopyImportedSource(glb, target);
		mesh.source             = source;

		if (!textureDir.empty())
			store.WriteTextures(imported, textureDir);

		auto outputs = store.WriteImportedRig(
			imported.skeleton,
			imported.animations,
			mesh,
			std::format("Derived/Skeletons/{}.bskel", name),
			std::format("Derived/Animations/{}.banim", name),
			true,
			source);

		if (!mesh.submeshes.empty())
			static_cast<void>(attachMaterial(mesh, 0, material));

		const std::string meshKey = std::format("Derived/Meshes/{}.bmesh", name);
		store.Save(mesh, meshKey);

		outputs.emplace_back(meshKey);
		target.skeleton = mesh.skeleton;
		target.outputs  = std::move(outputs);

		store.WriteImportedDocument(target, &mesh);
	}
}
