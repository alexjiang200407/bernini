#include "Import/import_writers.h"
#include <assetlib/bmesh.h>

#include <assetlib/asset_import.h>

#include "Windows/AssetImporter/material_stems.h"
#include "util/QtSupport.h"
#include "util/asset_paths.h"

#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include <catch2/catch_approx.hpp>

#include "StoreAt.h"
#include <QDir>
#include <assetlib/AssetStore.h>

namespace
{
	using assetlib::PbrChannel;

	/** A project tree that lasts as long as the test, under the OS temp directory. */
	class TempProject
	{
	public:
		TempProject()
		{
			m_Root = std::filesystem::temp_directory_path() /
			         ("bernini_import_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
			std::filesystem::create_directories(TextureDir());
		}

		~TempProject()
		{
			std::error_code ec;
			std::filesystem::remove_all(m_Root, ec);
		}

		TempProject(const TempProject&) = delete;
		TempProject&
		operator=(const TempProject&) = delete;

		[[nodiscard]] const std::filesystem::path&
		Data() const
		{
			return m_Root;
		}
		[[nodiscard]] std::filesystem::path
		MaterialDir() const
		{
			return m_Root / "Materials" / "hydrant";
		}
		[[nodiscard]] std::filesystem::path
		TextureDir() const
		{
			return m_Root / "Derived/SourceTextures" / "hydrant";
		}

	private:
		std::filesystem::path m_Root;
	};

	/**
	 * An import posing as a glTF with `materials` materials and one submesh per material, each cut from
	 * the material of the same index. No geometry: nothing here reads any.
	 */
	assetlib::imp::BMeshImport
	ImportWith(
		std::vector<assetlib::imp::BMaterialImport> materials,
		std::vector<std::string>                    names)
	{
		auto mesh = assetlib::imp::BMeshImport();

		for (size_t i = 0; i < materials.size(); ++i)
		{
			if (!names[i].empty())
			{
				materials[i].nameOffset = mesh.stringPool.add(names[i]);
			}

			auto submesh     = assetlib::Submesh();
			submesh.material = static_cast<uint32_t>(i);
			mesh.submeshes.push_back(submesh);
		}

		// The images the indices in PbrMaterial() address, named as a glTF names them -- the writer
		// routes at the file WriteTextures would give each, so a fixture with no images would route
		// every map at nothing.
		for (std::string_view name : { "albedo", "orm", "normal", "combined" })
		{
			mesh.textures.emplace_back();
			mesh.textureNames.emplace_back(name);
		}

		mesh.materials = std::move(materials);
		return mesh;
	}

	/**
	 * The stems the importer dialog would show for `imported`, and therefore the ones a real import
	 * hands the writer. A test that wants to pin a name types its own instead.
	 */
	QStringList
	StemsFor(const assetlib::imp::BMeshImport& imported)
	{
		auto probed = std::vector<assetlib::GltfMaterial>();
		probed.reserve(imported.materials.size());

		for (const assetlib::imp::BMaterialImport& source : imported.materials)
			probed.push_back(
				{ .name  = std::string(imported.stringPool.at(source.nameOffset)),
			      .isPbr = source.isPbr });

		return editor::MaterialStems(probed);
	}

	assetlib::imp::BMaterialImport
	PbrMaterial()
	{
		auto material             = assetlib::imp::BMaterialImport();
		material.baseColorTexture = 0;
		material.ormTexture       = 1;
		material.normalTexture    = 2;
		return material;
	}
}

TEST_CASE("An imported PBR material is written and bound to its submesh", "[importedmaterials]")
{
	const TempProject project;

	const auto imported = ImportWith({ PbrMaterial() }, { "Rust" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	// Named from the glTF, not by index: matN.bmaterial tells nobody anything.
	const std::filesystem::path file = project.MaterialDir() / "Rust.bmaterial";
	REQUIRE(std::filesystem::exists(file));

	// The mesh names it relative to the data root -- that is what makes a project relocatable.
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Materials/hydrant/Rust.bmaterial");
	CHECK(mesh.submeshes[0].material == 0);

	// And what landed is a material the renderer can draw, routed at this import's own textures.
	const assetlib::BMaterial material = LoadAt<assetlib::BMaterial>(file);
	CHECK(material.name == "Rust");
	CHECK(material.pbr.baseColorTexture.empty());  // no bake has run, so it draws from its routes
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kBaseColorR)].texture ==
		"Derived/SourceTextures/hydrant/albedo.ktx2");
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kMetallic)].texture ==
		"Derived/SourceTextures/hydrant/orm.ktx2");
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kNormalY)].texture ==
		"Derived/SourceTextures/hydrant/normal.ktx2");
	CHECK_FALSE(material.editorGraph.empty());
}

TEST_CASE("A non-PBR material is left behind, and its submesh unassigned", "[importedmaterials]")
{
	const TempProject project;

	auto unlit  = PbrMaterial();
	unlit.isPbr = false;

	const auto imported = ImportWith({ PbrMaterial(), unlit }, { "Metal", "Sign" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	CHECK(std::filesystem::exists(project.MaterialDir() / "Metal.bmaterial"));

	// Deriving a PBR material from one that declares another shading model would invent an authoring
	// intent the file never carried. Unassigned renders unlit, which is what it is.
	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "Sign.bmaterial"));
	CHECK(mesh.submeshes[1].material == assetlib::c_InvalidIndex);
	CHECK(mesh.materials.size() == 1);
}

TEST_CASE("Every derived material stem is one the dialog would accept", "[importedmaterials]")
{
	// The dialog seeds its name fields with these and then validates what they hold. Were the two to
	// disagree, a default name would open the dialog with OK already dead and nothing typed to fix.
	const auto probed = std::vector<assetlib::GltfMaterial>{
		{ .name = "Rust", .isPbr = true },
		{ .name = "Rust", .isPbr = true },
		{ .name = "", .isPbr = true },
		{ .name = "wood/oak", .isPbr = true },
		{ .name = "..", .isPbr = true },
		{ .name = "  spaced  ", .isPbr = true },
		{ .name = ".hidden", .isPbr = true },
		{ .name = "caf\xc3\xa9 noir", .isPbr = true },
		{ .name = "C:\\Windows", .isPbr = true },
		{ .name = "unlit", .isPbr = false },
	};

	const QStringList stems = editor::MaterialStems(probed);
	REQUIRE(stems.size() == static_cast<qsizetype>(probed.size()));

	for (qsizetype i = 0; i < stems.size(); ++i)
	{
		INFO("material " << i << " -> '" << stems[i].toStdString() << "'");

		if (!probed[static_cast<size_t>(i)].isPbr)
		{
			CHECK(stems[i].isEmpty());
			continue;
		}

		CHECK(editor::IsPlainFileStem(stems[i]));
	}
}

TEST_CASE("Imported material names are made safe and unique", "[importedmaterials]")
{
	const TempProject project;

	// A glTF material name is free text. Each of these would otherwise collide, escape the folder, or
	// name no file at all -- and every one of them is a name a real exporter produces.
	const auto imported = ImportWith(
		{ PbrMaterial(), PbrMaterial(), PbrMaterial(), PbrMaterial(), PbrMaterial() },
		{ "Rust", "Rust", "", "wood/oak", ".." });
	auto mesh = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	CHECK(std::filesystem::exists(project.MaterialDir() / "Rust.bmaterial"));
	CHECK(std::filesystem::exists(project.MaterialDir() / "Rust_2.bmaterial"));
	CHECK(std::filesystem::exists(project.MaterialDir() / "material2.bmaterial"));
	CHECK(std::filesystem::exists(project.MaterialDir() / "wood_oak.bmaterial"));
	CHECK(std::filesystem::exists(project.MaterialDir() / "material4.bmaterial"));

	// Five distinct files, all of them inside the import's own folder.
	const auto count =
		static_cast<size_t>(QDir(QString::fromStdWString(project.MaterialDir().wstring()))
	                            .entryList(QStringList{ "*.bmaterial" }, QDir::Files)
	                            .size());
	CHECK(count == 5);
	CHECK(mesh.materials.size() == 5);
}

TEST_CASE("A stem list that no longer fits the source is refused", "[importedmaterials]")
{
	const TempProject project;

	// The stems are chosen against a table probed before the dialog opens; the materials come from a
	// second parse after it closes. An artist re-exporting in between leaves them out of step, and
	// writing files named from the old table would bind materials to the wrong submeshes. Release
	// builds compile asserts out, so this has to be a refusal the import can report and roll back.
	const auto imported = ImportWith({ PbrMaterial(), PbrMaterial() }, { "Fur", "Eyes" });
	auto       mesh     = assetlib::toBMesh(imported);

	CHECK_THROWS_AS(
		editor::WriteImportedMaterials(
			imported,
			mesh,
			project.Data(),
			project.MaterialDir(),
			project.TextureDir(),
			QStringList{ "fur_brown" }),
		std::runtime_error);

	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "fur_brown.bmaterial"));
}

TEST_CASE("A material is written under the stem it was handed", "[importedmaterials]")
{
	const TempProject project;

	const auto imported = ImportWith({ PbrMaterial() }, { "Rust" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		QStringList{ "fur_brown" });

	// The name the dialog showed, not the one the glTF carried. Deriving it here as well is what
	// would let a preview and a file disagree, so the derived name must not appear at all.
	CHECK(std::filesystem::exists(project.MaterialDir() / "fur_brown.bmaterial"));
	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "Rust.bmaterial"));

	CHECK(mesh.materials[0] == "Materials/hydrant/fur_brown.bmaterial");
	CHECK(
		assetlib::AssetStore(project.MaterialDir())
			.Load<assetlib::BMaterial>("fur_brown.bmaterial")
			.name == "fur_brown");
}

TEST_CASE("A material with no stem is left behind", "[importedmaterials]")
{
	const TempProject project;

	// What the dialog produces for a material it does not offer to write. The submesh is left
	// unassigned, exactly as a non-PBR one is.
	const auto imported = ImportWith({ PbrMaterial(), PbrMaterial() }, { "Kept", "Dropped" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		QStringList{ "Kept", QString() });

	CHECK(std::filesystem::exists(project.MaterialDir() / "Kept.bmaterial"));
	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "Dropped.bmaterial"));
	CHECK(mesh.submeshes[1].material == assetlib::c_InvalidIndex);
}

TEST_CASE(
	"A failed import into a shared folder takes only its own materials",
	"[importedmaterials]")
{
	const TempProject project;

	// An import that has already landed in the folder.
	const auto first     = ImportWith({ PbrMaterial() }, { "Fur" });
	auto       firstMesh = assetlib::toBMesh(first);

	editor::WriteImportedMaterials(
		first,
		firstMesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		QStringList{ "fur_brown" });

	// A second one into the same folder -- what naming the files is for -- which then fails.
	const auto second     = ImportWith({ PbrMaterial() }, { "Fur" });
	auto       secondMesh = assetlib::toBMesh(second);

	editor::WriteImportedMaterials(
		second,
		secondMesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		QStringList{ "fur_grey" });

	const std::array<assetlib::ImportedFile, 1> written = { {
		{ project.MaterialDir() / "fur_grey.bmaterial", false },
	} };

	assetlib::rollBackImport(written, {});

	// The folder is not this import's to take down, so undoing it means removing exactly the files it
	// wrote -- the other import's work has to survive a failure that had nothing to do with it.
	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "fur_grey.bmaterial"));
	CHECK(std::filesystem::exists(project.MaterialDir() / "fur_brown.bmaterial"));
}

TEST_CASE("Two submeshes cut from one glTF material share its file", "[importedmaterials]")
{
	const TempProject project;

	auto imported = ImportWith({ PbrMaterial() }, { "Shared" });

	// A second submesh from the same material, as a multi-primitive mesh produces.
	auto second     = assetlib::Submesh();
	second.material = 0;
	imported.submeshes.push_back(second);

	auto mesh = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	// One material, named once: attachMaterial shares the slot rather than appending a duplicate, which
	// is what keeps the reference graph from reporting the mesh twice.
	CHECK(mesh.materials.size() == 1);
	CHECK(mesh.submeshes[0].material == 0);
	CHECK(mesh.submeshes[1].material == 0);
}

TEST_CASE("A cutout import survives the round-trip to disk", "[importedmaterials]")
{
	const TempProject project;

	auto leaves        = PbrMaterial();
	leaves.alphaMode   = assetlib::AlphaMode::kMask;
	leaves.alphaCutoff = 0.25f;

	const auto imported = ImportWith({ leaves }, { "Leaves" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	const assetlib::BMaterial material =
		assetlib::AssetStore(project.MaterialDir()).Load<assetlib::BMaterial>("Leaves.bmaterial");

	// The alpha mode is stored, not re-derived at load -- and the alpha is routed, which for a cutout
	// is the channel it cuts against.
	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kMask);
	CHECK(material.pbr.alphaCutoff == Catch::Approx(0.25f));
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kBaseColorA)].texture ==
		"Derived/SourceTextures/hydrant/albedo.ktx2");
	CHECK(material.pbr.routes[assetlib::channelIndex(PbrChannel::kBaseColorA)].channel == 3);
}

// The whole chain in one assertion: the glTF extension the importer reads, through the graph the
// sink rebuilds, to the .bmaterial the renderer loads. 0 is the value that matters -- every animal
// in the pack was authored with its specular switched off, and anywhere along here that drops it
// the surface gets a 0.04 sheen back with nothing in the file to say why.
TEST_CASE("An import's specular factors survive the round-trip to disk", "[importedmaterials]")
{
	const TempProject project;

	auto fur                = PbrMaterial();
	fur.specularFactor      = 0.0f;
	fur.specularColorFactor = glm::vec3(1.0f, 0.77f, 0.34f);

	const auto imported = ImportWith({ fur }, { "Fur" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	const assetlib::BMaterial material =
		assetlib::AssetStore(project.MaterialDir()).Load<assetlib::BMaterial>("Fur.bmaterial");

	CHECK(material.pbr.specularFactor == 0.0f);
	CHECK(material.pbr.specularColorFactor.g == Catch::Approx(0.77f));
	CHECK(material.pbr.specularColorFactor.b == Catch::Approx(0.34f));
}

TEST_CASE("One texture used as two maps routes both at the same file", "[importedmaterials]")
{
	const TempProject project;

	// The extractor deduplicates by image, so a glTF material naming one image for both base colour and
	// ORM arrives with both indices equal. Each map still gets its own node wired to its own port; the
	// routes just name the same file. (A real asset does this with a combined albedo/mask texture.)
	auto shared             = PbrMaterial();
	shared.baseColorTexture = 3;
	shared.ormTexture       = 3;
	shared.normalTexture    = assetlib::c_InvalidIndex;

	const auto imported = ImportWith({ shared }, { "Shared" });
	auto       mesh     = assetlib::toBMesh(imported);

	editor::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir(),
		StemsFor(imported));

	const assetlib::BMaterial material =
		assetlib::AssetStore(project.MaterialDir()).Load<assetlib::BMaterial>("Shared.bmaterial");

	// Base colour and ORM both name the same file, each reading the channels its own role wants.
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kBaseColorR)].texture ==
		"Derived/SourceTextures/hydrant/combined.ktx2");
	CHECK(
		material.pbr.routes[assetlib::channelIndex(PbrChannel::kAo)].texture ==
		"Derived/SourceTextures/hydrant/combined.ktx2");
	CHECK(material.pbr.routes[assetlib::channelIndex(PbrChannel::kAo)].channel == 0);
	CHECK(material.pbr.routes[assetlib::channelIndex(PbrChannel::kRoughness)].channel == 1);
	CHECK(material.pbr.routes[assetlib::channelIndex(PbrChannel::kNormalX)].texture.empty());
}
