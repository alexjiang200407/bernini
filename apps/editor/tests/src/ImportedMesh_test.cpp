#include "Windows/ContentExplorer/ContentExplorerWindow.h"

#include <assetlib_structs/BMesh.h>

namespace
{
	namespace fs = std::filesystem;

	/** Removes the test's temp tree even when the case fails by throwing. */
	struct TempTree
	{
		fs::path path;

		~TempTree()
		{
			std::error_code ec;
			fs::remove_all(path, ec);
		}
	};
}

// The mesh is the one import output whose directory nothing else creates: the rig, material and
// texture writes each make their own, so an import aimed at a fresh subfolder failed only at the
// .bmesh -- "cannot open file for writing".
TEST_CASE("A mesh import aimed at a new subfolder creates it", "[importedmesh]")
{
	int            tag = 0;
	const TempTree root{ fs::temp_directory_path() /
		                 ("bernini_mesh_test_" +
		                  std::to_string(reinterpret_cast<uintptr_t>(&tag))) };
	const fs::path bmeshPath = root.path / "Meshes" / "animals" / "unit.bmesh";

	const assetlib::BMesh mesh;
	ContentExplorerWindow::WriteImportedMesh(mesh, bmeshPath);

	CHECK(fs::exists(bmeshPath));
}
