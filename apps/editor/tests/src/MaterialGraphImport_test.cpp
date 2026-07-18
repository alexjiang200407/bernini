#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QJsonObject>

namespace
{
	using assetlib::PbrChannel;

	// Where an import puts things: the maps under textures_src/<model>/, the material in Materials/.
	// Nothing is written, so these need not exist -- Rebase is lexical.
	const auto    c_DataRoot  = std::filesystem::path("C:/proj/Data");
	const QString c_BaseColor = QStringLiteral("C:/proj/Data/textures_src/hydrant/tex0.ktx2");
	const QString c_Orm       = QStringLiteral("C:/proj/Data/textures_src/hydrant/tex1.ktx2");
	const QString c_Normal    = QStringLiteral("C:/proj/Data/textures_src/hydrant/tex2.ktx2");

	ImportedMaterialMaps
	AllMaps()
	{
		return ImportedMaterialMaps{ c_BaseColor, c_Normal, c_Orm };
	}

	const assetlib::ChannelRoute&
	Route(const assetlib::BMaterial& material, PbrChannel channel)
	{
		return material.pbr.routes[assetlib::ChannelIndex(channel)];
	}

	/** Builds the graph a glTF material implies and compiles it, exactly as an import does. */
	assetlib::BMaterial
	Import(const assetlib::imp::BMaterialImport& imported, const ImportedMaterialMaps& maps)
	{
		MaterialGraphModel model(MakeMaterialNodeRegistry(nullptr, nullptr));
		BuildImportedMaterialGraph(model, imported, maps);
		return CompileMaterial(model, QStringLiteral("hydrant"), c_DataRoot);
	}
}

TEST_CASE("An imported glTF material routes each map into its own channels", "[materialimport]")
{
	auto imported            = assetlib::imp::BMaterialImport();
	imported.baseColorFactor = glm::vec4(0.5f, 0.25f, 0.125f, 1.0f);
	imported.metallicFactor  = 0.0f;
	imported.roughnessFactor = 0.75f;

	const assetlib::BMaterial material = Import(imported, AllMaps());

	CHECK(material.shadingModel == assetlib::ShadingModel::kPbr);
	CHECK(material.mode == assetlib::MaterialMode::kLoose);

	// Relative to the data root, like every asset reference -- not to the material file.
	CHECK(Route(material, PbrChannel::kBaseColorR).texture == "textures_src/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kBaseColorR).channel == 0);
	CHECK(Route(material, PbrChannel::kBaseColorG).channel == 1);
	CHECK(Route(material, PbrChannel::kBaseColorB).channel == 2);

	// glTF packs occlusion/roughness/metallic into RGB, which is the ORM order.
	CHECK(Route(material, PbrChannel::kAo).texture == "textures_src/hydrant/tex1.ktx2");
	CHECK(Route(material, PbrChannel::kAo).channel == 0);
	CHECK(Route(material, PbrChannel::kRoughness).channel == 1);
	CHECK(Route(material, PbrChannel::kMetallic).channel == 2);

	CHECK(Route(material, PbrChannel::kNormalX).texture == "textures_src/hydrant/tex2.ktx2");
	CHECK(Route(material, PbrChannel::kNormalX).channel == 0);
	CHECK(Route(material, PbrChannel::kNormalY).channel == 1);

	CHECK(material.pbr.baseColorFactor.r == Catch::Approx(0.5f));
	CHECK(material.pbr.metallicFactor == Catch::Approx(0.0f));
	CHECK(material.pbr.roughnessFactor == Catch::Approx(0.75f));
}

TEST_CASE("An opaque import routes no alpha", "[materialimport]")
{
	auto imported      = assetlib::imp::BMaterialImport();
	imported.alphaMode = assetlib::AlphaMode::kOpaque;

	const assetlib::BMaterial material = Import(imported, AllMaps());

	// The opaque sink's base-colour port is 3-wide, so the alpha channel has nowhere to land. Routing
	// an alpha nothing tests against is what turns every material in a project into a BC7 cutout that
	// cuts nothing out -- see docs/asset_standards.md.
	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kOpaque);
	CHECK(Route(material, PbrChannel::kBaseColorA).texture.empty());
}

TEST_CASE("A cutout import routes the alpha it cuts against", "[materialimport]")
{
	auto imported        = assetlib::imp::BMaterialImport();
	imported.alphaMode   = assetlib::AlphaMode::kMask;
	imported.alphaCutoff = 0.25f;

	const assetlib::BMaterial material = Import(imported, AllMaps());

	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kMask);
	CHECK(material.pbr.alphaCutoff == Catch::Approx(0.25f));
	CHECK(Route(material, PbrChannel::kBaseColorA).texture == "textures_src/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kBaseColorA).channel == 3);
}

TEST_CASE("A blend import routes its alpha into a blend sink", "[materialimport]")
{
	auto imported      = assetlib::imp::BMaterialImport();
	imported.alphaMode = assetlib::AlphaMode::kBlend;

	// Built directly, to inspect the sink the import chose before compiling it.
	MaterialGraphModel model(MakeMaterialNodeRegistry(nullptr, nullptr));
	BuildImportedMaterialGraph(model, imported, AllMaps());

	REQUIRE(model.OutputNode() != nullptr);
	CHECK(model.OutputNode()->name() == QStringLiteral("BlendedMaterialOutput"));
	CHECK(model.OutputNode()->AlphaMode() == assetlib::AlphaMode::kBlend);
	// It is not the cutout sink: blend keeps the alpha but tests nothing against a cutoff.
	CHECK_FALSE(model.OutputNode()->IsAlphaTested());

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("hydrant"), c_DataRoot);

	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kBlend);

	// Blend reads the base-color alpha, like a cutout, so its 4-wide port routes channel 3.
	CHECK(Route(material, PbrChannel::kBaseColorA).texture == "textures_src/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kBaseColorA).channel == 3);
}

TEST_CASE("A map a glTF material does not name is left unrouted", "[materialimport]")
{
	auto imported = assetlib::imp::BMaterialImport();

	const assetlib::BMaterial material =
		Import(imported, ImportedMaterialMaps{ c_BaseColor, {}, {} });

	CHECK(Route(material, PbrChannel::kBaseColorR).texture == "textures_src/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kNormalX).texture.empty());
	CHECK(Route(material, PbrChannel::kAo).texture.empty());
}

TEST_CASE("An imported material reopens as the board that produced it", "[materialimport]")
{
	auto imported            = assetlib::imp::BMaterialImport();
	imported.alphaMode       = assetlib::AlphaMode::kMask;
	imported.alphaCutoff     = 0.4f;
	imported.baseColorFactor = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
	imported.metallicFactor  = 0.6f;
	imported.roughnessFactor = 0.7f;

	const assetlib::BMaterial material = Import(imported, AllMaps());

	// The whole point of writing editorGraph: reopening restores the board rather than a blank one. A
	// graph that compiled to different routes than the material shipped with would silently rewire the
	// material on the next Save, which is the failure this pins.
	REQUIRE_FALSE(material.editorGraph.empty());

	QJsonObject graph =
		QJsonDocument::fromJson(QByteArray::fromStdString(material.editorGraph)).object();
	REQUIRE_FALSE(graph.isEmpty());

	// A saved graph stores paths relative to the data root; a live one holds them absolute.
	RebaseGraphTextures(graph, c_DataRoot, false);

	MaterialGraphModel reopened(MakeMaterialNodeRegistry(nullptr, nullptr));
	reopened.load(graph);

	const assetlib::BMaterial recompiled =
		CompileMaterial(reopened, QStringLiteral("hydrant"), c_DataRoot);

	for (size_t i = 0; i < assetlib::c_LooseChannelCount; ++i)
	{
		CHECK(recompiled.pbr.routes[i].texture == material.pbr.routes[i].texture);
		CHECK(recompiled.pbr.routes[i].channel == material.pbr.routes[i].channel);
	}

	CHECK(recompiled.pbr.alphaMode == assetlib::AlphaMode::kMask);
	CHECK(recompiled.pbr.alphaCutoff == Catch::Approx(0.4f));
	CHECK(recompiled.pbr.metallicFactor == Catch::Approx(0.6f));
	CHECK(recompiled.pbr.roughnessFactor == Catch::Approx(0.7f));
}
