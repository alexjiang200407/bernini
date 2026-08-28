#include <assetlib/AssetStore.h>
#include <assetlib/container_info.h>
#include <assetlib/envmap.h>
#include <assetlib/pak.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BVat.h>
#include <core/file/LayeredFileSystem.h>
#include <core/file/LooseFileSystem.h>

#include "RefsSandbox.h"
#include "mounted_io.h"

using namespace assetlib;
using namespace assetlib::test;

TEST_CASE("a loose source reads and writes the same directory", "[assetsource]")
{
	const DataRoot root("assetsource_loose");
	WriteSource(root.path / "Derived/SourceTextures/skin.ktx2", { { 200, 180, 160, 255 } });
	BakeAndSave(root, "skin.bmaterial", "Derived/SourceTextures/skin.ktx2");

	const AssetStore store(root.path);

	CHECK(store.GetDataRoot() == root.path);
	CHECK(store.Exists("Authored/Materials/skin.bmaterial"));
	CHECK(
		store.Load<BMaterial>("Authored/Materials/skin.bmaterial").pbr.routes[0].texture ==
		"Derived/SourceTextures/skin.ktx2");
}

/**
 * A mount over a directory that is not there enumerates empty rather than failing, so without this
 * a mistyped root would read as a project with nothing in it -- a scan reporting no assets, a prune
 * reporting nothing to sweep. Both used to check for themselves; the source is where it belongs now
 * that they take one.
 */
// Every CLI command now names its asset with a mount key, so this is the one conversion standing
// between a typed argument and a write. A key that escaped the data root would let `tangents` or
// `exposure` rewrite a file the project does not own.
TEST_CASE("a write resolves to the data root, and cannot climb out of it", "[assetsource]")
{
	const DataRoot   root("assetsource_writepath");
	const AssetStore store(root.path);

	CHECK(store.ResolveWritePath("Derived/Meshes/a.bmesh") == root.path / "Derived/Meshes/a.bmesh");

	SECTION("a key is normalized on the way, so two spellings of one asset write to one file")
	{
		CHECK(
			store.ResolveWritePath("Derived/Meshes/../Meshes/a.bmesh") ==
			store.ResolveWritePath("Derived/Meshes/a.bmesh"));
		CHECK(
			store.ResolveWritePath("./Derived/Meshes/a.bmesh") ==
			store.ResolveWritePath("Derived/Meshes/a.bmesh"));
	}

	SECTION("anything that names something outside the project is refused")
	{
		CHECK_THROWS_AS(store.ResolveWritePath("../escaped.bmesh"), std::runtime_error);
		CHECK_THROWS_AS(
			store.ResolveWritePath("Derived/Meshes/../../../escaped.bmesh"),
			std::runtime_error);
		CHECK_THROWS_AS(store.ResolveWritePath(".."), std::runtime_error);
		CHECK_THROWS_AS(store.ResolveWritePath(""), std::runtime_error);

		// An absolute path is not a key at all, however plausible it looks.
		CHECK_THROWS_AS(store.ResolveWritePath("/etc/passwd"), std::runtime_error);
	}
}

TEST_CASE("a source over a directory that is not there is a caller error", "[assetsource]")
{
	const DataRoot root("assetsource_missing");

	CHECK_THROWS_AS(AssetStore(root.path / "nope"), std::runtime_error);

	// A file is not a directory either.
	std::ofstream(root.path / "a.txt") << "x";
	CHECK_THROWS_AS(AssetStore(root.path / "a.txt"), std::runtime_error);
}

TEST_CASE("a source must have somewhere to read", "[assetsource]")
{
	const DataRoot root("assetsource_null");

	CHECK_THROWS_AS(AssetStore(root.path, nullptr), std::runtime_error);
}

// The two halves are genuinely two things: what a read resolves through can be wider than what a
// write lands on, and that is the whole point of the overlay.
TEST_CASE("reads widen to the mount while writes stay on the data root", "[assetsource]")
{
	const DataRoot root("assetsource_overlay");
	WriteSource(root.path / "Derived/SourceTextures/skin.ktx2", { { 200, 180, 160, 255 } });
	BakeAndSave(root, "packed.bmaterial", "Derived/SourceTextures/skin.ktx2");

	static_cast<void>(AssetStore(root.path).Pack(PackDesc{ root.path / "Data.bpak" }));

	// Gone from the writable layer, still in the archive.
	fs::remove(root.path / "Authored/Materials/packed.bmaterial");

	auto mount = std::make_shared<core::file::LayeredFileSystem>();
	mount->Mount(std::make_shared<core::file::LooseFileSystem>(root.path));
	mount->Mount(std::make_shared<PakFile>(root.path / "Data.bpak"));

	const AssetStore store(root.path, mount);

	CHECK(store.Exists("Authored/Materials/packed.bmaterial"));
	CHECK(store.GetDataRoot() == root.path);
	CHECK_FALSE(fs::exists(store.GetDataRoot() / "Authored/Materials/packed.bmaterial"));

	// And the read goes through: the archive answers what the directory no longer can.
	CHECK(
		store.Load<BMaterial>("Authored/Materials/packed.bmaterial").pbr.routes[0].texture ==
		"Derived/SourceTextures/skin.ktx2");
}

// The methods are the same answers the free functions give, addressed through the source rather
// than through a mount the caller had to carry beside a root.
TEST_CASE("the staleness methods answer as the free functions do", "[assetsource]")
{
	const DataRoot root("assetsource_staleness");
	WriteSource(root.path / "Derived/SourceTextures/skin.ktx2", { { 200, 180, 160, 255 } });
	const BMaterial material =
		BakeAndSave(root, "skin.bmaterial", "Derived/SourceTextures/skin.ktx2");

	const AssetStore                  store(root.path);
	const core::file::LooseFileSystem loose(root.path);

	CHECK(store.BakeIsStale(material) == bakeIsStale(material, loose));
	CHECK(store.DrawsLoose(material) == drawsLoose(material, loose));
	CHECK(
		store.StampOf("Derived/SourceTextures/skin.ktx2") ==
		stampOf(loose, "Derived/SourceTextures/skin.ktx2"));

	// The env predicates forward too, and the gate says "the staleness methods" rather than three of
	// them.
	BSky sky;
	sky.sky.source = "Derived/SourceTextures/skin.ktx2";
	CHECK(store.IsSkyBakeStale(sky) == isSkyBakeStale(sky, loose));
	CHECK(store.EnvMapToDraw(sky.sky) == envMapToDraw(sky.sky, loose));

	BEnvLighting lighting;
	lighting.prefilter.source  = "Derived/SourceTextures/skin.ktx2";
	lighting.irradiance.source = "Derived/SourceTextures/skin.ktx2";
	CHECK(store.IsEnvLightingBakeStale(lighting) == isEnvLightingBakeStale(lighting, loose));

	BVat vat;
	vat.mesh = "Derived/Meshes/gone.bmesh";
	CHECK(store.VatIsStale(vat) == vatIsStale(vat, loose));

	// An absent path is an answer, not a failure.
	CHECK(store.StampOf("Derived/SourceTextures/gone.ktx2") == SourceStamp{});
	CHECK_FALSE(store.Exists("Derived/SourceTextures/gone.ktx2"));
}
