#include "Windows/AssetImporter/AssetImporterDialog.h"
#include "util/QtSupport.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh_gltf.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
	constexpr auto c_SourceFile = "C:/Assets/Exports/stone_wall.glb";
	std::vector<assetlib::GltfMaterial>
	Probe(size_t total, size_t pbr)
	{
		auto result = std::vector<assetlib::GltfMaterial>(total);
		for (size_t i = 0; i < total; ++i)
		{
			result[i].name  = "material" + std::to_string(i);
			result[i].isPbr = i < pbr;
		}
		return result;
	}
	QLineEdit*
	Field(const AssetImporterDialog& dialog, const char* name)
	{
		return dialog.findChild<QLineEdit*>(name);
	}
	QCheckBox*
	Box(const AssetImporterDialog& dialog, const char* name)
	{
		return dialog.findChild<QCheckBox*>(name);
	}
	bool
	CanAccept(const AssetImporterDialog& dialog)
	{
		return dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled();
	}
}

TEST_CASE("The importer offers only authored destination fields", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));
	for (const auto* name : { "meshFolder",
	                          "meshName",
	                          "skeletonFolder",
	                          "skeletonName",
	                          "animationFolder",
	                          "animationName",
	                          "textureFolder" })
		CHECK(Field(dialog, name) == nullptr);
	REQUIRE(Field(dialog, "sourceFolder") != nullptr);
	REQUIRE(Field(dialog, "sourceName") != nullptr);
	REQUIRE(Field(dialog, "materialFolder") != nullptr);
	const auto outputs = dialog.GetOutputs();
	CHECK(outputs.source == "Authored/Meshes/stone_wall.glb");
	CHECK(outputs.identity.id != 0);
	CHECK(outputs.identity.label == "stone_wall.glb");
	CHECK(
		outputs.mesh.toStdString() ==
		assetlib::importOutputKey(outputs.identity, assetlib::AssetType::kMesh));
	CHECK(
		outputs.skeleton.toStdString() ==
		assetlib::importOutputKey(outputs.identity, assetlib::AssetType::kSkeleton));
	CHECK(
		outputs.animations.toStdString() ==
		assetlib::importOutputKey(outputs.identity, assetlib::AssetType::kAnimation));
	CHECK(outputs.textureDir.toStdString() == assetlib::importTextureDirectory(outputs.identity));
	CHECK(outputs.materialDir == "Authored/Materials/stone_wall");
	CHECK(outputs.materialStems == QStringList{ "material0", "material1" });
	CHECK(dialog.GetOutputs().identity == outputs.identity);
	CHECK(CanAccept(dialog));
}

TEST_CASE("Same-named sources get distinct generated outputs", "[assetimporter]")
{
	const AssetImporterDialog first(c_SourceFile);
	const AssetImporterDialog second(c_SourceFile);
	Field(first, "sourceFolder")->setText("town");
	Field(second, "sourceFolder")->setText("props");
	CHECK(first.GetOutputs().identity.id != second.GetOutputs().identity.id);
	CHECK(first.GetOutputs().mesh != second.GetOutputs().mesh);
	CHECK(first.GetOutputs().textureDir != second.GetOutputs().textureDir);
	const auto before = first.GetOutputs();
	Field(first, "sourceFolder")->setText("elsewhere");
	CHECK(first.GetOutputs().mesh == before.mesh);
	CHECK(first.GetOutputs().identity == before.identity);
}

TEST_CASE("Material names unfold and remain independently authorable", "[assetimporter]")
{
	AssetImporterDialog dialog(c_SourceFile, Probe(12, 12));
	dialog.show();
	REQUIRE(editor::test::WaitFor([&] { return dialog.isVisible(); }));
	REQUIRE(dialog.grab().save("asset_importer.got.png"));
	CHECK_FALSE(Field(dialog, "materialName0")->isVisible());
	dialog.findChild<QToolButton*>("materialFolderToggle")->click();
	CHECK(Field(dialog, "materialName11")->isVisible());
	const auto mesh = dialog.GetOutputs().mesh;
	Field(dialog, "materialFolder")->setText("shared/walls");
	Field(dialog, "materialName0")->setText("brick");
	CHECK(dialog.GetOutputs().materialDir == "Authored/Materials/shared/walls");
	CHECK(dialog.GetOutputs().materialStems.front() == "brick");
	CHECK(dialog.GetOutputs().mesh == mesh);
}

TEST_CASE("Material import requires PBR materials, textures and geometry", "[assetimporter]")
{
	const auto                probe = GENERATE(Probe(0, 0), Probe(3, 0), Probe(3, 1));
	const AssetImporterDialog dialog(c_SourceFile, probe);
	const bool                available = probe.size() == 3 && probe.front().isPbr;
	CHECK(dialog.CanImportPbrMaterials() == available);
	CHECK(dialog.GetImportTextures());
	CHECK_FALSE(dialog.GetImportAnimations());
	Box(dialog, "importTextures")->setChecked(false);
	CHECK_FALSE(dialog.CanImportPbrMaterials());
	Box(dialog, "importTextures")->setChecked(true);
	CHECK(dialog.CanImportPbrMaterials() == available);
	Box(dialog, "importMesh")->setChecked(false);
	CHECK_FALSE(dialog.CanImportPbrMaterials());
	CHECK_FALSE(dialog.GetImportMesh());
}

TEST_CASE("Skipped source materials have no authored name", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(3, 1));
	CHECK(Field(dialog, "materialName0") != nullptr);
	CHECK(Field(dialog, "materialName1") == nullptr);
	CHECK(dialog.GetOutputs().materialStems == QStringList{ "material0", QString(), QString() });
}

TEST_CASE("Duplicate authored material names refuse the import", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(2, 2));
	Field(dialog, "materialName0")->setText("Rust");
	Field(dialog, "materialName1")->setText(GENERATE("Rust", "rust"));
	CHECK_FALSE(CanAccept(dialog));
	Box(dialog, "importPbrMaterials")->setChecked(false);
	CHECK(CanAccept(dialog));
}

TEST_CASE("Invalid source names refuse every import that copies a source", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile);
	Field(dialog, "sourceName")
		->setText(GENERATE("", "   ", ".", "..", "walls/stone", "C:/Windows/hal", "..\\system"));
	CHECK_FALSE(CanAccept(dialog));
	Box(dialog, "importMesh")->setChecked(false);
	CHECK_FALSE(CanAccept(dialog));
	CHECK(Field(dialog, "sourceFolder")->isEnabled());
	Field(dialog, "sourceName")->setText("stone");
	CHECK(CanAccept(dialog));
	Box(dialog, "importTextures")->setChecked(false);
	CHECK_FALSE(CanAccept(dialog));
}

TEST_CASE("Source and sidecar collisions are refused before import", "[assetimporter]")
{
	QTemporaryDir root;
	REQUIRE(root.isValid());
	REQUIRE(QDir(root.path()).mkpath("Authored/Meshes/exterior"));
	const QString extension = GENERATE(QString(".glb"), QString(".bimport"));
	QFile         taken(root.path() + "/Authored/Meshes/exterior/stone_wall" + extension);
	REQUIRE(taken.open(QIODevice::WriteOnly));
	taken.close();
	const AssetImporterDialog dialog(c_SourceFile, {}, root.path());
	Field(dialog, "sourceFolder")->setText("exterior");
	CHECK_FALSE(CanAccept(dialog));
	CHECK(dialog.GetProblem().contains(extension));
	Field(dialog, "sourceName")->setText("other");
	CHECK(CanAccept(dialog));
}

TEST_CASE("Source and material folders stay inside their authored categories", "[assetimporter]")
{
	const AssetImporterDialog dialog(c_SourceFile, Probe(1, 1));
	const auto                typed = GENERATE(
		"..",
		"../../Windows",
		"walls/../../../Windows",
		"C:/Windows/System32",
		"/etc",
		"\\Windows",
		"D:walls");
	Field(dialog, "sourceFolder")->setText(typed);
	Field(dialog, "materialFolder")->setText(typed);
	CHECK(dialog.GetOutputs().source == "Authored/Meshes/stone_wall.glb");
	CHECK(dialog.GetOutputs().materialDir == "Authored/Materials/stone_wall");
}
