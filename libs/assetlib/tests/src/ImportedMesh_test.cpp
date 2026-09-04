
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMesh.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

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

// An import aimed at a fresh subfolder writes two levels of it that nothing scaffolded. What that
// costs when the write does not make them is a failure inside write_atomic naming a temp file,
// which reads as a permissions problem rather than a missing directory.
TEST_CASE("A mesh import aimed at a new subfolder creates it", "[importedmesh]")
{
	int            tag = 0;
	const TempTree root{ fs::temp_directory_path() /
		                 ("bernini_mesh_test_" +
		                  std::to_string(reinterpret_cast<uintptr_t>(&tag))) };

	// The data root itself, which a real project always has -- Project::Create scaffolds it, and
	// AssetStore refuses one that is not there. What this case is about is the *subfolder*.
	fs::create_directories(root.path);

	const fs::path bmeshPath = root.path / "Derived/Meshes" / "animals" / "unit.bmesh";

	const assetlib::BMesh mesh;
	// The store's own root, so the key is what the import writes and the path is what it lands at.
	assetlib::AssetStore(root.path).Save(mesh, "Derived/Meshes/animals/unit.bmesh");

	CHECK(fs::exists(bmeshPath));
}
