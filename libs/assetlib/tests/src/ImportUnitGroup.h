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
	 * Imports `glb` into `dataRoot` as the group named "unit", by the same writers the CLI and
	 * the editor call: the copied source and its document in `meshes_src/`, the mesh in
	 * `Meshes/`, and -- when the source carries a skin -- the rig in `Skeletons/` and
	 * `Animations/`. Submesh 0 is bound to `material`, recorded in the document.
	 */
	inline void
	ImportUnitGroup(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& glb,
		std::string_view             material   = "Materials/red.bmaterial",
		float                        sampleRate = c_DefaultSampleRate)
	{
		const auto imported = loadFromGltf(glb, { .sampleRate = sampleRate });

		BMesh mesh = toBMesh(imported);
		generateTangents(mesh);
		requireUniqueSubmeshNames(mesh);

		const ImportTarget target{ dataRoot, "unit", sampleRate };
		const SourceRef    source = copyImportedSource(glb, target);
		mesh.source               = source;

		const AssetStore store(dataRoot);

		store.WriteImportedRig(
			imported.skeleton,
			imported.animations,
			mesh,
			"Skeletons/unit.bskel",
			"Animations/unit.banim",
			true,
			source);

		if (!mesh.submeshes.empty())
			static_cast<void>(attachMaterial(mesh, 0, material));

		store.Save(mesh, "Meshes/unit.bmesh");
		writeImportedDocument(target, &mesh);
	}
}
