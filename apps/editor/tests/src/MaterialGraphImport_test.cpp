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

	// Where an import puts things: the maps under Derived/SourceTextures/<model>/, the material in
	// Authored/Materials/.
	// Nothing is written, so these need not exist -- Rebase is lexical.
	const auto    c_DataRoot = std::filesystem::path("C:/proj/Data");
	const QString c_BaseColor =
		QStringLiteral("C:/proj/Data/Derived/SourceTextures/hydrant/tex0.ktx2");
	const QString c_Orm = QStringLiteral("C:/proj/Data/Derived/SourceTextures/hydrant/tex1.ktx2");
	const QString c_Normal =
		QStringLiteral("C:/proj/Data/Derived/SourceTextures/hydrant/tex2.ktx2");

	ImportedMaterialMaps
	AllMaps()
	{
		return ImportedMaterialMaps{ c_BaseColor, c_Normal, c_Orm };
	}

	const assetlib::ChannelRoute&
	Route(const assetlib::BMaterial& material, PbrChannel channel)
	{
		return material.pbr.routes[assetlib::channelIndex(channel)];
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

TEST_CASE("Opening a material does not round its factors", "[materialimport]")
{
	// The factor spin boxes show three decimals and round what they are given to them. Unblocked,
	// setValue reported the rounded number straight back through valueChanged, so merely opening a
	// material and saving it turned a roughness of 0.8585786 into 0.859 -- permanently, and for
	// every factor a glTF import wrote at full precision.
	constexpr float c_Roughness = 0.8585786f;
	constexpr float c_Metallic  = 0.1234567f;

	MaterialOutputNode node;

	QJsonObject saved;
	saved[QStringLiteral("roughness")] = c_Roughness;
	saved[QStringLiteral("metallic")]  = c_Metallic;

	// The widgets exist only once the node has been shown, and it is their sync that rounded.
	REQUIRE(node.embeddedWidget() != nullptr);
	node.load(saved);

	CHECK(node.RoughnessFactor() == c_Roughness);
	CHECK(node.MetallicFactor() == c_Metallic);
}

TEST_CASE("Two orderings of one board serialise the same", "[materialimport]")
{
	// QtNodes keeps connections in an unordered set, so what `save()` emits follows insertion --
	// and a board built by an import, one loaded from a file and one a person edited all insert
	// differently. Same board, different bytes, so the .bmaterial rewrote itself on every save with
	// no change in it: a diff on every open, and a byte-compare `migrate` can no longer trust.
	const auto board = [](const char* connections, const char* nodes) {
		return QJsonDocument::fromJson(
				   QByteArray("{\"connections\":") + connections + ",\"nodes\":" + nodes + "}")
		    .object();
	};

	const char* forward  = R"([{"inNodeId":0,"inPortIndex":0,"outNodeId":1,"outPortIndex":1},)"
						   R"({"inNodeId":0,"inPortIndex":2,"outNodeId":2,"outPortIndex":2}])";
	const char* reversed = R"([{"inNodeId":0,"inPortIndex":2,"outNodeId":2,"outPortIndex":2},)"
						   R"({"inNodeId":0,"inPortIndex":0,"outNodeId":1,"outPortIndex":1}])";
	const char* inOrder  = R"([{"id":2},{"id":0},{"id":1}])";
	const char* shuffled = R"([{"id":1},{"id":2},{"id":0}])";

	QJsonObject one = board(forward, inOrder);
	QJsonObject two = board(reversed, shuffled);
	REQUIRE(
		QJsonDocument(one).toJson(QJsonDocument::Compact) !=
		QJsonDocument(two).toJson(QJsonDocument::Compact));

	SortGraph(one);
	SortGraph(two);

	CHECK(
		QJsonDocument(one).toJson(QJsonDocument::Compact) ==
		QJsonDocument(two).toJson(QJsonDocument::Compact));
}

TEST_CASE("A compiled board comes out ordered", "[materialimport]")
{
	assetlib::imp::BMaterialImport imported;
	imported.baseColorTexture = 0;
	imported.normalTexture    = 1;
	imported.ormTexture       = 2;

	const QJsonObject graph =
		QJsonDocument::fromJson(QByteArray::fromStdString(Import(imported, AllMaps()).editorGraph))
			.object();

	QJsonObject sorted = graph;
	SortGraph(sorted);
	CHECK(
		QJsonDocument(graph).toJson(QJsonDocument::Compact) ==
		QJsonDocument(sorted).toJson(QJsonDocument::Compact));
}

TEST_CASE("An imported glTF material routes each map into its own channels", "[materialimport]")
{
	auto imported            = assetlib::imp::BMaterialImport();
	imported.baseColorFactor = glm::vec4(0.5f, 0.25f, 0.125f, 1.0f);
	imported.metallicFactor  = 0.0f;
	imported.roughnessFactor = 0.75f;

	const assetlib::BMaterial material = Import(imported, AllMaps());

	CHECK(material.shadingModel == assetlib::ShadingModel::kPbr);

	// No bake has run, so there is no triplet and the material draws from its routes.
	CHECK(material.pbr.baseColorTexture.empty());

	// Relative to the data root, like every asset reference -- not to the material file.
	CHECK(
		Route(material, PbrChannel::kBaseColorR).texture ==
		"Derived/SourceTextures/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kBaseColorR).channel == 0);
	CHECK(Route(material, PbrChannel::kBaseColorG).channel == 1);
	CHECK(Route(material, PbrChannel::kBaseColorB).channel == 2);

	// glTF packs occlusion/roughness/metallic into RGB, which is the ORM order.
	CHECK(Route(material, PbrChannel::kAo).texture == "Derived/SourceTextures/hydrant/tex1.ktx2");
	CHECK(Route(material, PbrChannel::kAo).channel == 0);
	CHECK(Route(material, PbrChannel::kRoughness).channel == 1);
	CHECK(Route(material, PbrChannel::kMetallic).channel == 2);

	CHECK(
		Route(material, PbrChannel::kNormalX).texture ==
		"Derived/SourceTextures/hydrant/tex2.ktx2");
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
	CHECK(
		Route(material, PbrChannel::kBaseColorA).texture ==
		"Derived/SourceTextures/hydrant/tex0.ktx2");
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
	CHECK(model.OutputNode()->GetAlphaMode() == assetlib::AlphaMode::kBlend);
	// It is not the cutout sink: blend keeps the alpha but tests nothing against a cutoff.
	CHECK_FALSE(model.OutputNode()->IsAlphaTested());

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("hydrant"), c_DataRoot);

	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kBlend);

	// Blend reads the base-color alpha, like a cutout, so its 4-wide port routes channel 3.
	CHECK(
		Route(material, PbrChannel::kBaseColorA).texture ==
		"Derived/SourceTextures/hydrant/tex0.ktx2");
	CHECK(Route(material, PbrChannel::kBaseColorA).channel == 3);
}

// The import's transmission has to survive the trip out through the graph and back, because the
// graph is what the compile reads -- a factor the sink dropped would leave a lens importing as the
// coverage material it is not, and no later edit would say why.
TEST_CASE("A blend import carries its transmission through the graph", "[materialimport]")
{
	auto imported               = assetlib::imp::BMaterialImport();
	imported.alphaMode          = assetlib::AlphaMode::kBlend;
	imported.transmissionFactor = 0.85f;

	MaterialGraphModel model(MakeMaterialNodeRegistry(nullptr, nullptr));
	BuildImportedMaterialGraph(model, imported, AllMaps());

	REQUIRE(model.OutputNode() != nullptr);
	CHECK(model.OutputNode()->GetTransmission() == Catch::Approx(0.85f));

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("hydrant"), c_DataRoot);

	CHECK(material.pbr.transmissionFactor == Catch::Approx(0.85f));
}

// The sink is what CompileMaterial reads, so a factor the node does not hold is silently reset the
// first time an imported material is opened and saved -- and a squirrel whose author switched the
// specular off gets its sheen back with nothing in the file to say why.
TEST_CASE("An import carries its specular factors through the graph", "[materialimport]")
{
	auto imported                = assetlib::imp::BMaterialImport();
	imported.specularFactor      = 0.0f;
	imported.specularColorFactor = glm::vec3(1.0f, 0.77f, 0.34f);

	MaterialGraphModel model(MakeMaterialNodeRegistry(nullptr, nullptr));
	BuildImportedMaterialGraph(model, imported, AllMaps());

	REQUIRE(model.OutputNode() != nullptr);
	CHECK(model.OutputNode()->GetSpecularFactor() == 0.0f);

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("squirrel"), c_DataRoot);

	CHECK(material.pbr.specularFactor == 0.0f);
	CHECK(material.pbr.specularColorFactor.g == Catch::Approx(0.77f));

	// An opaque sink carries them as much as any other: specular is not a property of the alpha mode.
	CHECK(material.pbr.alphaMode == assetlib::AlphaMode::kOpaque);
}

// A graph saved before the specular keys existed carries none of them, and must load as glTF's
// defaults rather than as whatever the sink happened to hold.
TEST_CASE("A graph with no specular keys loads at the defaults", "[materialimport]")
{
	MaterialGraphModel    model(MakeMaterialNodeRegistry(nullptr, nullptr));
	const QtNodes::NodeId id = model.addNode(QStringLiteral("MaterialOutput"));

	auto* output = model.delegateModel<MaterialOutputNode>(id);
	REQUIRE(output != nullptr);

	auto factors        = QJsonObject();
	factors["metallic"] = 0.25;
	output->load(factors);

	CHECK(output->GetSpecularFactor() == 1.0f);
	CHECK(output->GetSpecularColorFactor() == glm::vec3(1.0f));
}

// Every sink but the blend one compiles to a mode that never reads transmission, and a graph saved
// before the factor existed carries no key for it. Both must land on 0 rather than on whatever the
// sink happened to hold -- that is what leaves hair, foliage and every material baked so far
// rendering as they did.
TEST_CASE("A material with no transmission of its own compiles to none", "[materialimport]")
{
	MaterialGraphModel    model(MakeMaterialNodeRegistry(nullptr, nullptr));
	const QtNodes::NodeId id     = model.addNode(QStringLiteral("BlendedMaterialOutput"));
	auto*                 output = model.delegateModel<MaterialOutputNode>(id);
	REQUIRE(output != nullptr);

	// A graph from before the factor: the key is simply absent.
	REQUIRE_NOTHROW(output->load(QJsonObject()));
	CHECK(output->GetTransmission() == 0.0f);

	const assetlib::BMaterial compiled = CompileMaterial(model, QStringLiteral("m"), c_DataRoot);
	CHECK(compiled.pbr.transmissionFactor == 0.0f);

	// And the opaque sink has no transmission to give at all.
	MaterialGraphModel    opaque(MakeMaterialNodeRegistry(nullptr, nullptr));
	const QtNodes::NodeId opaqueId = opaque.addNode(QStringLiteral("MaterialOutput"));
	REQUIRE(opaque.delegateModel<MaterialOutputNode>(opaqueId) != nullptr);
	CHECK(opaque.delegateModel<MaterialOutputNode>(opaqueId)->GetTransmission() == 0.0f);
}

// A graph saved while the blend sink still had an Occlude toggle carries `occlude` and
// `alphaCutoff` keys. Loading one must not fail or resurrect the setting -- the pre-pass it selected
// no longer exists, and hashed alpha is what a self-occluding surface uses now.
TEST_CASE("A blend graph saved with the retired occlude keys still loads", "[materialimport]")
{
	MaterialGraphModel    model(MakeMaterialNodeRegistry(nullptr, nullptr));
	const QtNodes::NodeId id     = model.addNode(QStringLiteral("BlendedMaterialOutput"));
	auto*                 output = model.delegateModel<MaterialOutputNode>(id);
	REQUIRE(output != nullptr);

	auto saved           = QJsonObject();
	saved["occlude"]     = true;
	saved["alphaCutoff"] = 0.2;
	REQUIRE_NOTHROW(output->load(saved));

	CHECK(output->GetAlphaMode() == assetlib::AlphaMode::kBlend);

	const assetlib::BMaterial compiled = CompileMaterial(model, QStringLiteral("m"), c_DataRoot);
	CHECK(compiled.pbr.alphaMode == assetlib::AlphaMode::kBlend);
}

TEST_CASE("A map a glTF material does not name is left unrouted", "[materialimport]")
{
	auto imported = assetlib::imp::BMaterialImport();

	const assetlib::BMaterial material =
		Import(imported, ImportedMaterialMaps{ c_BaseColor, {}, {} });

	CHECK(
		Route(material, PbrChannel::kBaseColorR).texture ==
		"Derived/SourceTextures/hydrant/tex0.ktx2");
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
