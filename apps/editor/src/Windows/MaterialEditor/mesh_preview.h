#pragma once

#include <QStringList>

#include <assetlib_structs/BMesh.h>
#include <bgl/PreparedStaticMesh.h>
#include <gamelib/Raycaster.h>

namespace editor
{
	/** One mesh entry the preview's nodes reference: cooked for the scene, copied for picking. */
	struct PreparedPreviewMesh
	{
		// Move-only, because the cook is; the build is /Wall /WX, where leaving that implicit warns.
		PreparedPreviewMesh()  = default;
		~PreparedPreviewMesh() = default;

		PreparedPreviewMesh(PreparedPreviewMesh&&) = default;

		PreparedPreviewMesh&
		operator=(PreparedPreviewMesh&&) = default;

		PreparedPreviewMesh(const PreparedPreviewMesh&) = delete;

		PreparedPreviewMesh&
		operator=(const PreparedPreviewMesh&) = delete;

		uint32_t                meshIndex = 0;
		bgl::PreparedStaticMesh cooked;

		// Its index in the build's raycaster, which an instance of this entry is placed against.
		uint32_t raycastGeometry = 0;
	};

	/** Where one node stands an instance of `entry`, an index into MeshPreviewBuild::meshes. */
	struct PreviewPlacement
	{
		uint32_t  entry = 0;
		glm::mat4 world = glm::mat4(1.0f);
	};

	/**
	 * One submesh of the preview, as the selector addresses it: which prepared entry it belongs to,
	 * where it sits inside that entry, and where it sat in the source `.bmesh`.
	 *
	 * Deliberately the same fields as MaterialPreviewWindow::SubmeshRef, which is what the window
	 * copies these into -- the window's own type cannot be named here without including it.
	 */
	struct PreviewSubmeshRef
	{
		uint32_t entry         = 0;
		uint32_t localSubmesh  = 0;
		uint32_t sourceSubmesh = 0;
		bool     hasTangent    = false;
	};

	/** Everything a mesh preview needs before the scene is touched. Move-only: the cooks are. */
	struct MeshPreviewBuild
	{
		std::vector<PreparedPreviewMesh> meshes;
		std::vector<PreviewPlacement>    placements;

		// Already holding every geometry and every instance, ready to answer a pick.
		game::Raycaster raycaster;

		// Parallel, one entry per submesh, in the order the selector lists them.
		QStringList                    submeshNames;
		QStringList                    submeshMaterialPaths;
		std::vector<PreviewSubmeshRef> submeshRefs;
	};

	/**
	 * Everything standing `mesh` up in the preview costs except the upload: one meshlet cook per
	 * mesh entry a node references, the CPU picking copy of each, and the selector's submesh table.
	 *
	 * None of it touches bgl's scene or Qt's widgets, so it belongs on a worker -- fused into the
	 * commit it rode the render thread, which stops the frame loop for every viewport in the editor.
	 *
	 * A mesh is cooked once however many nodes instance it, and `placements` carries one entry per
	 * node that does.
	 *
	 * @param dataRoot The project's Data directory, which the material paths resolve against. Empty
	 *        leaves them empty, which is what a closed project means.
	 * @throws std::runtime_error if no node references a mesh; bgl::SceneError for anything
	 *         CookStaticMesh refuses.
	 */
	[[nodiscard]] MeshPreviewBuild
	PrepareMeshPreview(const assetlib::BMesh& mesh, const std::filesystem::path& dataRoot);
}
