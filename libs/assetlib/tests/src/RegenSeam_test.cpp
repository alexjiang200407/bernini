#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "MountAt.h"
#include "SkinnedGltf.h"

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	/**
	 * A project holding one imported group, built by the same writers the CLI and the editor call.
	 * The glb import runs once per case, which is what keeps every case's disk state its own.
	 */
	struct ImportedProject
	{
		Project  project;
		fs::path dataRoot;
		fs::path meshPath;
		fs::path documentPath;

		explicit ImportedProject(const char* name, const fs::path& glb, float sampleRate = 30.0f) :
			project(MakeProject(name))
		{
			dataRoot = project.GetDataDirectory();

			test::ImportUnitGroup(dataRoot, glb, "Materials/red.bmaterial", sampleRate);

			meshPath     = dataRoot / c_MeshesDirectoryName / "unit.bmesh";
			documentPath = dataRoot / "meshes_src/unit.bimport";

			project.ReloadStore();
		}

		[[nodiscard]] const AssetStore&
		Store() const
		{
			return project.GetStore();
		}

		/** The name binding the document's first submesh entry carries. */
		[[nodiscard]] std::string
		FirstSubmeshName() const
		{
			const BMesh mesh = LoadAt<BMesh>(meshPath);
			return std::string(mesh.stringPool.at(mesh.submeshes.at(0).nameOffset));
		}

		void
		Tamper(const fs::path& path, size_t offset) const
		{
			test::TamperHeaderByte(path, offset);
		}

	private:
		static Project
		MakeProject(const char* name)
		{
			const fs::path root = fs::temp_directory_path() / name;
			fs::remove_all(root);
			return Project::Create(root / "Regen.berniniproject", "Regen");
		}
	};

	std::vector<std::byte>
	BytesOf(const fs::path& path)
	{
		return core::file::read_file_bytes(path.string());
	}

	/** A loose mount reporting itself read-only -- a stand-in for an archive. */
	class ReadOnlyFileSystem final : public core::file::IFileSystem
	{
	public:
		explicit ReadOnlyFileSystem(const fs::path& root) : m_Inner(root) {}

		ReadOnlyFileSystem(const ReadOnlyFileSystem&) = delete;
		ReadOnlyFileSystem(ReadOnlyFileSystem&&)      = delete;
		ReadOnlyFileSystem&
		operator=(const ReadOnlyFileSystem&) = delete;
		ReadOnlyFileSystem&
		operator=(ReadOnlyFileSystem&&) = delete;

		[[nodiscard]] bool
		Exists(std::string_view path) const noexcept override
		{
			return m_Inner.Exists(path);
		}

		[[nodiscard]] std::optional<core::file::FileStamp>
		Stat(std::string_view path) const noexcept override
		{
			return m_Inner.Stat(path);
		}

		[[nodiscard]] std::vector<std::byte>
		Read(std::string_view path) const override
		{
			return m_Inner.Read(path);
		}

		[[nodiscard]] std::vector<std::byte>
		ReadRange(std::string_view path, uint64_t offset, uint64_t size) const override
		{
			return m_Inner.ReadRange(path, offset, size);
		}

		[[nodiscard]] std::vector<std::string>
		Enumerate(std::string_view prefix = {}) const override
		{
			return m_Inner.Enumerate(prefix);
		}

		[[nodiscard]] bool
		IsReadOnly() const noexcept override
		{
			return true;
		}

	private:
		core::file::LooseFileSystem m_Inner;
	};
}

TEST_CASE("a fresh entry loads untouched, its document applied", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_fresh", "assets/apples.glb");
	const auto            before = BytesOf(sandbox.meshPath);

	const RegenMesh current = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
	CHECK(current.unboundBindings.empty());
	CHECK(current.mesh.materials == std::vector<std::string>{ "Materials/red.bmaterial" });

	CHECK(BytesOf(sandbox.meshPath) == before);
}

TEST_CASE("a binding-only document edit rebinds the loaded mesh without regeneration", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_rebind", "assets/apples.glb");
	const auto            before = BytesOf(sandbox.meshPath);

	AssetStore(sandbox.dataRoot)
		.RebindSubmeshInDocument(
			"meshes_src/unit.glb",
			sandbox.FirstSubmeshName(),
			"Materials/blue.bmaterial");

	const RegenMesh current = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
	CHECK(
		current.mesh.materials.at(current.mesh.submeshes.at(0).material) ==
		"Materials/blue.bmaterial");
	CHECK(BytesOf(sandbox.meshPath) == before);

	SECTION("and rebinds a group whose source file is gone, which regeneration could not serve")
	{
		fs::remove(sandbox.dataRoot / "meshes_src/unit.glb");
		AssetStore(sandbox.dataRoot)
			.RebindSubmeshInDocument(
				"meshes_src/unit.glb",
				sandbox.FirstSubmeshName(),
				"Materials/green.bmaterial");

		const RegenMesh again = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
		CHECK(
			again.mesh.materials.at(again.mesh.submeshes.at(0).material) ==
			"Materials/green.bmaterial");
		CHECK(BytesOf(sandbox.meshPath) == before);
	}
}

TEST_CASE("a stale bake token regenerates the mesh from its source", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_token", "assets/apples.glb");
	const BMesh           fresh = LoadAt<BMesh>(sandbox.meshPath);

	sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);
	const auto stale = BytesOf(sandbox.meshPath);

	// The plain load refuses; the seam serves the source's current cook instead.
	CHECK_THROWS_WITH(
		sandbox.Store().Load<BMesh>("Meshes/unit.bmesh"),
		Catch::Matchers::ContainsSubstring("another bake revision"));

	const RegenMesh current = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
	CHECK(current.mesh.submeshes.size() == fresh.submeshes.size());
	CHECK(current.mesh.vertexData == fresh.vertexData);
	CHECK(current.mesh.materials == fresh.materials);  // the document's bindings, applied
	CHECK(current.mesh.source.stamp == fresh.source.stamp);

	// In memory only: the stale file is migrate's to rewrite, never a load's.
	CHECK(BytesOf(sandbox.meshPath) == stale);
}

TEST_CASE(
	"a recorded stamp that no longer matches the source regenerates -- the merge property",
	"[regen]")
{
	const ImportedProject sandbox("bernini_regen_stamp", "assets/apples.glb");
	const BMesh           fresh = LoadAt<BMesh>(sandbox.meshPath);

	// A sibling branch's binary swapped in by a merge: current token, foreign source stamp.
	sandbox.Tamper(sandbox.meshPath, test::c_SourceHashOffset);

	const RegenMesh current = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
	CHECK(current.mesh.vertexData == fresh.vertexData);
	CHECK(current.mesh.source.stamp == fresh.source.stamp);
}

TEST_CASE("a stale entry that cannot regenerate refuses", "[regen]")
{
	SECTION("no source was ever recorded")
	{
		const ImportedProject sandbox("bernini_regen_norecord", "assets/apples.glb");

		BMesh synthetic  = LoadAt<BMesh>(sandbox.meshPath);
		synthetic.source = SourceRef();
		SaveAt(synthetic, sandbox.meshPath);
		sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);

		CHECK_THROWS_WITH(
			sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh"),
			Catch::Matchers::ContainsSubstring("no source was ever recorded"));
	}

	SECTION("the recorded source is gone from the project")
	{
		const ImportedProject sandbox("bernini_regen_nosource", "assets/apples.glb");

		fs::remove(sandbox.dataRoot / "meshes_src/unit.glb");
		sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);

		CHECK_THROWS_WITH(
			sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh"),
			Catch::Matchers::ContainsSubstring("not in the project"));
	}

	SECTION("the import document is gone, so the parameters are unknowable")
	{
		const ImportedProject sandbox("bernini_regen_nodocregen", "assets/apples.glb");

		fs::remove(sandbox.documentPath);
		sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);

		CHECK_THROWS_WITH(
			sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh"),
			Catch::Matchers::ContainsSubstring("import document"));
	}
}

TEST_CASE("a read-only store trusts its keys and its baked bindings", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_readonly", "assets/apples.glb");

	// Stale by stamp, and rebound in the document: a writable store would act on both.
	sandbox.Tamper(sandbox.meshPath, test::c_SourceHashOffset);
	AssetStore(sandbox.dataRoot)
		.RebindSubmeshInDocument(
			"meshes_src/unit.glb",
			sandbox.FirstSubmeshName(),
			"Materials/blue.bmaterial");

	const AssetStore readOnly(
		sandbox.dataRoot,
		std::make_shared<ReadOnlyFileSystem>(sandbox.dataRoot));
	const RegenMesh current = readOnly.LoadRegenMesh("Meshes/unit.bmesh");
	CHECK(current.mesh.materials == std::vector<std::string>{ "Materials/red.bmaterial" });
	CHECK(current.unboundBindings.empty());
}

TEST_CASE("a stale rig regenerates, and its clips follow the document's sample rate", "[regen]")
{
	const test::SkinnedGltf source("bernini_regen_rig_gltf");
	const ImportedProject   sandbox("bernini_regen_rig", source.PackGlb());

	const fs::path bskelPath = sandbox.dataRoot / c_SkeletonsDirectoryName / "unit.bskel";
	const Skeleton fresh     = LoadAt<Skeleton>(bskelPath);

	SECTION("the skeleton")
	{
		sandbox.Tamper(bskelPath, test::c_TokenOffset);

		const Skeleton skeleton = sandbox.Store().LoadRegenSkeleton("Skeletons/unit.bskel");
		REQUIRE(skeleton.bones.size() == fresh.bones.size());
		CHECK(skeletonSignature(skeleton) == skeletonSignature(fresh));
		CHECK(skeleton.source.key == "meshes_src/unit.glb");
	}

	SECTION("the clips, at the rate the document says")
	{
		auto document = loadImportDocument(
			core::file::LooseFileSystem(sandbox.dataRoot),
			"meshes_src/unit.bimport");
		document.sampleRate = 60.0f;
		core::file::write_atomic(
			sandbox.documentPath,
			AssetCodec<ImportDocument>::Serialize(document));

		// The mesh goes stale too, so the disk walk cannot serve its box: the one under test is
		// the box the seam measures from the regenerated form.
		sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);

		// The parameter edit alone is the staleness: the token and the source still match.
		const AnimationSet clips = sandbox.Store().LoadRegenAnimations("Animations/unit.banim");
		REQUIRE(clips.clips.size() == 2);
		CHECK(clips.clips[0].sampleRate == 60.0f);
		CHECK(clips.skeleton == "Skeletons/unit.bskel");
		CHECK(clips.skeletonSignature == skeletonSignature(fresh));

		// The posed boxes were re-measured, not lost -- #413's shape is a box baked from the
		// wrong data -- and under the signature a consumer computes: the mesh as it loads,
		// tangents and all. A box nobody can find is a load measuring at draw time instead.
		REQUIRE_FALSE(clips.posedBoxes.empty());
		const RegenMesh held = sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh");
		CHECK(findPosedBounds(clips, held.mesh, fresh)[0].has_value());
	}

	SECTION("a re-exported source that dropped its mesh is reported, not served")
	{
		const test::SkinnedGltf meshless(
			"bernini_regen_meshless_gltf",
			{ { "\"mesh\": 0, \"skin\": 0, \"name\": \"body\"", "\"name\": \"body\"" },
		      { "\"meshes\": [ { \"name\": \"body\", \"primitives\": [ {\n"
		        "    \"attributes\": { \"POSITION\": 0, \"JOINTS_0\": 1, \"WEIGHTS_0\": 2 },\n"
		        "    \"indices\": 3, \"mode\": 4 } ] } ],",
		        "\"meshes\": []," } });
		fs::copy_file(
			meshless.PackGlb(),
			sandbox.dataRoot / "meshes_src/unit.glb",
			fs::copy_options::overwrite_existing);

		CHECK_THROWS_WITH(
			sandbox.Store().LoadRegenMesh("Meshes/unit.bmesh"),
			Catch::Matchers::ContainsSubstring("no longer carries a mesh"));
	}

	SECTION("a re-exported source that dropped its rig is reported, not served")
	{
		const test::SkinnedGltf boneless(
			"bernini_regen_boneless_gltf",
			{ { "\"skins\": [ { \"joints\": [ 2, 1 ], \"inverseBindMatrices\": 4 } ],", "" },
		      { "\"mesh\": 0, \"skin\": 0, \"name\": \"body\"",
		        "\"mesh\": 0, \"name\": \"body\"" } });
		fs::copy_file(
			boneless.PackGlb(),
			sandbox.dataRoot / "meshes_src/unit.glb",
			fs::copy_options::overwrite_existing);

		CHECK_THROWS_WITH(
			sandbox.Store().LoadRegenSkeleton("Skeletons/unit.bskel"),
			Catch::Matchers::ContainsSubstring("no longer carries a rig"));
	}
}

TEST_CASE("reauthor rewrites a document from its mesh, once", "[regen][importdoc]")
{
	const ImportedProject sandbox("bernini_regen_reauthor", "assets/apples.glb");

	// A rebind saved straight into the mesh, the way every save worked before documents: the
	// document still records red, the mesh now says blue.
	BMesh mesh = LoadAt<BMesh>(sandbox.meshPath);
	REQUIRE(attachMaterial(mesh, 0, "Materials/blue.bmaterial"));
	SaveAt(mesh, sandbox.meshPath);

	const auto report = AssetStore(sandbox.dataRoot).ReauthorImportDocuments();
	REQUIRE(report.size() == 1);
	CHECK(report[0].key == "meshes_src/unit.bimport");
	CHECK(report[0].outcome == ReauthoredDocument::Outcome::kRewritten);

	const ImportDocument document = loadImportDocument(
		core::file::LooseFileSystem(sandbox.dataRoot),
		"meshes_src/unit.bimport");
	CHECK(document.sampleRate == 30.0f);
	REQUIRE_FALSE(document.bindings.empty());
	CHECK(document.bindings[0].material == "Materials/blue.bmaterial");

	SECTION("a second run rewrites nothing")
	{
		const auto again = AssetStore(sandbox.dataRoot).ReauthorImportDocuments();
		REQUIRE(again.size() == 1);
		CHECK(again[0].outcome == ReauthoredDocument::Outcome::kUnchanged);
	}

	SECTION("a clips-only document keeps its empty bindings")
	{
		AssetStore(sandbox.dataRoot)
			.WriteImportedDocument(ImportTarget{ "clipsonly", 30.0f }, nullptr);

		const auto again = AssetStore(sandbox.dataRoot).ReauthorImportDocuments();
		REQUIRE(again.size() == 2);
		for (const ReauthoredDocument& entry : again)
			CHECK(entry.outcome == ReauthoredDocument::Outcome::kUnchanged);
	}

	SECTION("an unreadable mesh header fails a claimless document rather than clearing it")
	{
		AssetStore(sandbox.dataRoot)
			.WriteImportedDocument(ImportTarget{ "clipsonly", 30.0f }, nullptr);
		core::file::write_atomic(sandbox.meshPath, std::string_view("not a mesh"));

		const auto again = AssetStore(sandbox.dataRoot).ReauthorImportDocuments();
		REQUIRE(again.size() == 2);
		for (const ReauthoredDocument& entry : again)
		{
			CHECK(entry.outcome == ReauthoredDocument::Outcome::kFailed);
			CHECK_THAT(entry.message, Catch::Matchers::ContainsSubstring("unreadable"));
		}

		// The bindings the pass could not attribute stand exactly as they were.
		CHECK_FALSE(loadImportDocument(sandbox.documentPath).bindings.empty());
	}
}

TEST_CASE("a rebind with no document to land in is refused", "[regen][importdoc]")
{
	const ImportedProject sandbox("bernini_regen_nodoc", "assets/apples.glb");
	fs::remove(sandbox.documentPath);

	CHECK_THROWS_WITH(
		AssetStore(sandbox.dataRoot)
			.RebindSubmeshInDocument(
				"meshes_src/unit.glb",
				sandbox.FirstSubmeshName(),
				"Materials/blue.bmaterial"),
		Catch::Matchers::ContainsSubstring("no import document"));
}

TEST_CASE("GeometryIsStale answers the key without loading a payload", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_isstale", "assets/apples.glb");

	CHECK_FALSE(sandbox.Store().GeometryIsStale("Meshes/unit.bmesh"));

	sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);
	CHECK(sandbox.Store().GeometryIsStale("Meshes/unit.bmesh"));

	SECTION("a read-only store trusts its keys")
	{
		const AssetStore readOnly(
			sandbox.dataRoot,
			std::make_shared<ReadOnlyFileSystem>(sandbox.dataRoot));
		CHECK_FALSE(readOnly.GeometryIsStale("Meshes/unit.bmesh"));
	}

	SECTION("a container without a cache key is refused, not guessed at")
	{
		CHECK_THROWS_WITH(
			sandbox.Store().GeometryIsStale("meshes_src/unit.bimport"),
			Catch::Matchers::ContainsSubstring("no cache key"));
	}
}

TEST_CASE("skipping textures still imports the geometry and the rig", "[regen][gltf]")
{
	const auto full    = loadFromGltf("assets/apples.glb");
	const auto skipped = loadFromGltf("assets/apples.glb", { .textures = GltfTextures::kSkip });

	CHECK(skipped.submeshes.size() == full.submeshes.size());
	CHECK(skipped.vertexData == full.vertexData);
	CHECK(skipped.textures.empty());
	CHECK(skipped.materials.empty());  // they exist to route the textures
}

TEST_CASE("a foreign-token mesh still answers a reference scan from its headers", "[regen]")
{
	const ImportedProject sandbox("bernini_regen_refscan", "assets/apples.glb");
	sandbox.Tamper(sandbox.meshPath, test::c_TokenOffset);

	// No regeneration behind this: the materials are the document's and the rig answers by
	// source key, so a post-token-bump scan stays a header read per file.
	const MeshRefs refs = sandbox.Store().LoadRegenMeshRefs("Meshes/unit.bmesh");
	CHECK(refs.materials == std::vector<std::string>{ "Materials/red.bmaterial" });
	CHECK(refs.skeleton.empty());  // apples carries no rig
}
