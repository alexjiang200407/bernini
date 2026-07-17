#include "Windows/ContentExplorer/ContentExplorerWindow.h"

#include "util/QtSupport.h"

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>

#include <catch2/catch_approx.hpp>

#include <QDir>

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
			return m_Root / "textures_src" / "hydrant";
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
		mesh.stringPool.push_back('\0');  // offset 0 is the empty string

		for (size_t i = 0; i < materials.size(); ++i)
		{
			if (!names[i].empty())
			{
				materials[i].nameOffset = static_cast<uint32_t>(mesh.stringPool.size());
				mesh.stringPool.insert(mesh.stringPool.end(), names[i].begin(), names[i].end());
				mesh.stringPool.push_back('\0');
			}

			auto submesh     = assetlib::Submesh();
			submesh.material = static_cast<uint32_t>(i);
			mesh.submeshes.push_back(submesh);
		}

		mesh.materials = std::move(materials);
		return mesh;
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

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

	// Named from the glTF, not by index: matN.bmaterial tells nobody anything.
	const std::filesystem::path file = project.MaterialDir() / "Rust.bmaterial";
	REQUIRE(std::filesystem::exists(file));

	// The mesh names it relative to the data root -- that is what makes a project relocatable.
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Materials/hydrant/Rust.bmaterial");
	CHECK(mesh.submeshes[0].material == 0);

	// And what landed is a material the renderer can draw, routed at this import's own textures.
	const assetlib::BMaterial material = assetlib::loadMaterial(file);
	CHECK(material.name == "Rust");
	CHECK(material.mode == assetlib::MaterialMode::kLoose);
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kBaseColorR)].texture ==
		"textures_src/hydrant/tex0.ktx2");
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kMetallic)].texture ==
		"textures_src/hydrant/tex1.ktx2");
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kNormalY)].texture ==
		"textures_src/hydrant/tex2.ktx2");
	CHECK_FALSE(material.editorGraph.empty());
}

TEST_CASE("A non-PBR material is left behind, and its submesh unassigned", "[importedmaterials]")
{
	const TempProject project;

	auto unlit  = PbrMaterial();
	unlit.isPbr = false;

	const auto imported = ImportWith({ PbrMaterial(), unlit }, { "Metal", "Sign" });
	auto       mesh     = assetlib::toBMesh(imported);

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

	CHECK(std::filesystem::exists(project.MaterialDir() / "Metal.bmaterial"));

	// Deriving a PBR material from one that declares another shading model would invent an authoring
	// intent the file never carried. Unassigned renders unlit, which is what it is.
	CHECK_FALSE(std::filesystem::exists(project.MaterialDir() / "Sign.bmaterial"));
	CHECK(mesh.submeshes[1].material == assetlib::c_InvalidIndex);
	CHECK(mesh.materials.size() == 1);
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

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

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

TEST_CASE("Two submeshes cut from one glTF material share its file", "[importedmaterials]")
{
	const TempProject project;

	auto imported = ImportWith({ PbrMaterial() }, { "Shared" });

	// A second submesh from the same material, as a multi-primitive mesh produces.
	auto second     = assetlib::Submesh();
	second.material = 0;
	imported.submeshes.push_back(second);

	auto mesh = assetlib::toBMesh(imported);

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

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

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

	const assetlib::BMaterial material =
		assetlib::loadMaterial(project.MaterialDir() / "Leaves.bmaterial");

	// The alpha mode is stored, not re-derived at load -- and the alpha is routed, which for a cutout
	// is the channel it cuts against.
	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kMask);
	CHECK(material.pbr.alphaCutoff == Catch::Approx(0.25f));
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kBaseColorA)].texture ==
		"textures_src/hydrant/tex0.ktx2");
	CHECK(material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kBaseColorA)].channel == 3);
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

	ContentExplorerWindow::WriteImportedMaterials(
		imported,
		mesh,
		project.Data(),
		project.MaterialDir(),
		project.TextureDir());

	const assetlib::BMaterial material =
		assetlib::loadMaterial(project.MaterialDir() / "Shared.bmaterial");

	// Base colour and ORM both name tex3, each reading the channels its own role wants.
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kBaseColorR)].texture ==
		"textures_src/hydrant/tex3.ktx2");
	CHECK(
		material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kAo)].texture ==
		"textures_src/hydrant/tex3.ktx2");
	CHECK(material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kAo)].channel == 0);
	CHECK(material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kRoughness)].channel == 1);
	CHECK(material.pbr.routes[assetlib::ChannelIndex(PbrChannel::kNormalX)].texture.empty());
}
