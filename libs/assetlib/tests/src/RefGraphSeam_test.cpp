#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/pak_io.h>
#include <assetlib/pak_pack.h>
#include <assetlib/texture_prune.h>
#include <core/file/LayeredFileSystem.h>
#include <core/file/LooseFileSystem.h>

#include "CountingFileSystem.h"
#include "MountAt.h"
#include "RefsSandbox.h"
#include "ref_paths.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	// A mesh, the material it names, that material's source and baked triplet, and a whole
	// environment -- enough kinds that every collector runs.
	Environment
	StageProject(const DataRoot& root)
	{
		WriteSource(root.path / "textures_src/skin.ktx2", { { 200, 180, 160, 255 } });
		BakeAndSave(root, "skin.bmaterial", "textures_src/skin.ktx2");
		SaveMesh(root, "hero.bmesh", { "Materials/skin.bmaterial" });

		return WriteEnvironment(root);
	}

	void
	Pack(const DataRoot& root)
	{
		static_cast<void>(AssetStore(root.path).Pack(PackDesc{ root.path / "Data.bpak" }));
	}

	bool
	Proposes(const TexturePruneScan& scan, std::string_view map)
	{
		return std::ranges::any_of(scan.unused, [&map](const UnusedTexture& unused) {
			return unused.path == map;
		});
	}

	// AssetStore takes its mount by shared_ptr, so a test builds one rather than pointing at a local.
	AssetStore
	Archived(const DataRoot& root)
	{
		return AssetStore(root.path, std::make_shared<PakFile>(root.path / "Data.bpak"));
	}

	// The overlay the editor writes: loose over the archive, writes landing loose.
	AssetStore
	Overlaid(const DataRoot& root)
	{
		auto mount = std::make_shared<core::file::LayeredFileSystem>();
		mount->Mount(std::make_shared<core::file::LooseFileSystem>(root.path));
		mount->Mount(std::make_shared<PakFile>(root.path / "Data.bpak"));

		return AssetStore(root.path, std::move(mount));
	}

	std::vector<AssetRef>
	EdgesOf(const AssetRefGraph& graph)
	{
		const std::span<const AssetRef> edges = graph.Edges();
		return std::vector<AssetRef>(edges.begin(), edges.end());
	}
}

/**
 * The archive must not change what the graph says. Everything downstream of it -- what a delete
 * refuses, what a rename rewrites, what a prune sweeps -- is decided by these edges, so a project
 * that answered differently once packed would make every one of those operations a different
 * operation on a shipped build.
 */
TEST_CASE(
	"the graph over an archive equals the graph over the tree it was packed from",
	"[refseam]")
{
	const DataRoot root("refseam_equal");
	StageProject(root);
	Pack(root);

	const AssetRefGraph direct = AssetRefGraph::Scan(AssetStore(root.path));

	const AssetRefGraph packed = AssetRefGraph::Scan(Archived(root));

	CHECK(EdgesOf(packed) == EdgesOf(direct));

	CHECK(packed.meshesScanned == direct.meshesScanned);
	CHECK(packed.materialsScanned == direct.materialsScanned);
	CHECK(packed.environmentsScanned == direct.environmentsScanned);

	// The edges match, but what resolves does not, and that is the mount answering rather than the
	// disk: `textures_src` is never packed, so inside the archive those sources are simply absent.
	// A `broken` set still read off the data root would call them present and disagree with the
	// graph it is part of.
	CHECK(direct.broken.empty());
	CHECK_FALSE(packed.broken.empty());

	for (const AssetRef& edge : packed.broken) CHECK(edge.target.starts_with("textures_src/"));
}

// The extension of a mount key, read off the key rather than through std::filesystem::path -- the
// conversion STYLE.md's Paths section warns turns a key into one an archive lookup misses.
TEST_CASE("extensionOf answers what a path would, without becoming one", "[refseam]")
{
	CHECK(extensionOf("Materials/skin.bmaterial") == ".bmaterial");
	CHECK(extensionOf("Textures/SKIN.KTX2") == ".ktx2");
	CHECK(extensionOf("Meshes/hero") == "");

	// A dot in a directory name does not extend the file inside it.
	CHECK(extensionOf("a.b/c") == "");

	// A leading dot names the file rather than extending it, as path::extension also answers.
	CHECK(extensionOf(".overlay.json") == ".json");
	CHECK(extensionOf(".gitignore") == "");
	CHECK(extensionOf("Data/.gitignore") == "");
}

TEST_CASE("GetFilesUnder names a directory's contents and nothing beside it", "[refseam]")
{
	const DataRoot root("refseam_filesunder");
	WriteSource(root.path / "textures_src/kirk/a.ktx2", { { 1, 2, 3, 255 } });
	WriteSource(root.path / "textures_src/kirk2/b.ktx2", { { 4, 5, 6, 255 } });

	const AssetRefGraph graph = AssetRefGraph::Scan(AssetStore(root.path));

	const std::vector<std::string> kirk = { "textures_src/kirk/a.ktx2" };

	CHECK(graph.GetFilesUnder("textures_src/kirk") == kirk);

	// A sibling whose name this one is a prefix of must not be swept in with it.
	CHECK(
		graph.GetFilesUnder("textures_src/kirk2") ==
		std::vector<std::string>{ "textures_src/kirk2/b.ktx2" });

	// Normalized like every other query on the graph, so a trailing separator is not a different
	// directory -- it would otherwise build a `//` prefix and match nothing.
	CHECK(graph.GetFilesUnder("textures_src/kirk/") == kirk);
	CHECK(graph.GetFilesUnder("./textures_src/kirk") == kirk);

	CHECK(graph.GetFilesUnder("textures_src/nothing").empty());
}

// A plan can name an asset the archive holds and the loose tree never had. Unlinking it is what
// task 10's tombstone is for; until then the failure has to be said, because `remove` reports
// success for a path that was never there.
TEST_CASE("deleting an asset that only the archive holds is refused", "[refseam]")
{
	const DataRoot root("refseam_delete_packed");
	WriteSource(root.path / "textures_src/skin.ktx2", { { 200, 180, 160, 255 } });
	BakeAndSave(root, "skin.bmaterial", "textures_src/skin.ktx2");
	Pack(root);

	fs::remove(root.path / "Materials/skin.bmaterial");

	const AssetStore store = Overlaid(root);

	const AssetRefGraph graph = AssetRefGraph::Scan(store);
	const DeletionPlan  plan  = planDeletion(graph, "Materials/skin.bmaterial");
	REQUIRE(plan.Allowed());

	const DeletionResult result = store.DeleteAsset(plan);
	CHECK(result.status == DeletionStatus::kFailed);
}

// The same rule for the two other sets a plan can name: a directory's members, and what a cascade
// frees. `remove_all` on a directory that was never there reports no error either.
TEST_CASE("deleting a directory the archive alone holds is refused", "[refseam]")
{
	const DataRoot root("refseam_delete_packed_dir");
	WriteSource(root.path / "textures_src/skin.ktx2", { { 1, 2, 3, 255 } });

	// Under Materials/, which packing carries -- textures_src is excluded by the exclusion rule, so
	// a directory there would be absent from the archive too and prove nothing.
	fs::create_directories(root.path / "Materials/kirk");
	BakeAndSave(root, "kirk/Body.bmaterial", "textures_src/skin.ktx2");
	Pack(root);

	fs::remove_all(root.path / "Materials/kirk");

	const AssetStore store = Overlaid(root);

	const AssetRefGraph graph = AssetRefGraph::Scan(store);
	const DeletionPlan  plan  = planDeletion(graph, "Materials/kirk");

	REQUIRE(plan.IsDirectory());
	REQUIRE(plan.contents == std::vector<std::string>{ "Materials/kirk/Body.bmaterial" });

	CHECK(store.DeleteAsset(plan).status == DeletionStatus::kFailed);
}

// The overlay the editor writes: a packed asset and an edited loose copy of it are one asset, not
// two, and the loose one is what the scan reads.
TEST_CASE("a loose copy shadows its packed twin, and is scanned once", "[refseam]")
{
	const DataRoot root("refseam_shadow");
	StageProject(root);
	Pack(root);

	// Re-point the mesh at a different material, loose only.
	SaveMesh(root, "hero.bmesh", { "Materials/edited.bmaterial" });

	core::file::LayeredFileSystem mount;
	mount.Mount(std::make_shared<core::file::LooseFileSystem>(root.path));
	mount.Mount(std::make_shared<PakFile>(root.path / "Data.bpak"));

	const AssetRefGraph graph = AssetRefGraph::Scan(Overlaid(root));

	CHECK(graph.meshesScanned == 1);

	const std::vector<std::string> referrers = ReferrerPaths(graph, "Materials/edited.bmaterial");
	CHECK(referrers == std::vector<std::string>{ "Meshes/hero.bmesh" });

	// The packed edge is gone, not merely outvoted: one mesh was scanned, and it named one material.
	CHECK(graph.ReferrersOf("Materials/skin.bmaterial").empty());
}

/**
 * The prune's two halves read different things on purpose: it marks over the whole mount and sweeps
 * only the writable layer.
 *
 * Marking only what can be deleted would sweep every map whose sole referrer is packed. Sweeping the
 * whole mount would propose deleting entries inside an archive, which nothing can carry out.
 */
TEST_CASE("a prune over a mount union proposes only what it could delete", "[refseam][prune]")
{
	const DataRoot root("refseam_prune");
	WriteSource(root.path / "textures_src/skin.ktx2", { { 200, 180, 160, 255 } });
	const BMaterial packedMaterial =
		BakeAndSave(root, "packed.bmaterial", "textures_src/skin.ktx2");
	Pack(root);

	SECTION("a map held alive only by a packed material is not swept")
	{
		// The material is deleted from the loose tree; only the archive still names its triplet.
		fs::remove(root.path / "Materials/packed.bmaterial");

		const TexturePruneScan scan = Overlaid(root).FindUnusedBakedTextures();

		// The archive still names it, so it is live however the loose tree looks.
		CHECK_FALSE(Proposes(scan, packedMaterial.pbr.baseColorTexture));

		// And the sweep did run: the candidate was seen and spared, not simply never looked at.
		CHECK(scan.candidates != 0);
	}

	// The same question without the mount: nothing holds the triplet any more, so it is garbage.
	// This is what the section above would report if the mark phase read the writable layer alone.
	SECTION("without the archive in the mount, the same map is swept")
	{
		fs::remove(root.path / "Materials/packed.bmaterial");

		const TexturePruneScan scan = AssetStore(root.path).FindUnusedBakedTextures();

		CHECK(Proposes(scan, packedMaterial.pbr.baseColorTexture));
	}
}

/**
 * The reference scan reads a header, a chunk table and two small chunks per mesh, never the
 * geometry. #351 measured that on `loadMeshRefs` because the scan did not sit behind the seam yet;
 * this is the assertion that task meant to make.
 */
TEST_CASE("scanning a project reads references and not geometry", "[refseam]")
{
	const DataRoot root("refseam_reads");
	StageProject(root);

	// Bulky enough that reading it whole would be unmistakable beside the reference chunks.
	BMesh heavy = MakeMesh({ "Materials/skin.bmaterial" });
	heavy.vertexData.resize(512u * 1024u, std::byte{ 0x7 });
	heavy.indexData.resize(128u * 1024u, std::byte{ 0x3 });
	StoreAt(root.path).Save(heavy, "Meshes/heavy.bmesh");

	const uint64_t meshBytes = std::filesystem::file_size(root.path / "Meshes/heavy.bmesh");
	REQUIRE(meshBytes > 512u * 1024u);

	const auto loose    = std::make_shared<core::file::LooseFileSystem>(root.path);
	const auto counting = std::make_shared<CountingFileSystem>(*loose);

	static_cast<void>(AssetRefGraph::Scan(AssetStore(root.path, counting)));

	// Every other asset in the project is a few hundred bytes, so the mesh's geometry would dominate
	// this total if it were being read at all.
	CHECK(counting->bytesRead < meshBytes / 4);
}
