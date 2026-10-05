#include "Windows/MeshEditor/MaterialGraphModel.h"
#include "Windows/MeshEditor/graph_compiler.h"
#include "Windows/MeshEditor/material_graph.h"
#include "Windows/MeshEditor/nodes/MaterialOutputNode.h"
#include "Windows/MeshEditor/nodes/MaterialSinkNode.h"
#include "Windows/MeshEditor/nodes/SurfaceOutputNode.h"
#include "Windows/MeshEditor/nodes/TextureNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>

#include "util/QtSupport.h"  // IWYU pragma: keep
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <bgl/types/SurfaceMaterialDesc.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QPushButton>
#include <QSignalSpy>
#include <QWidget>
#include <QtNodes/NodeDelegateModelRegistry>
#include <bgl/glm.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <filesystem>
#include <memory>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <utility>

namespace
{
	using QtNodes::ConnectionId;
	using QtNodes::InvalidNodeId;
	using QtNodes::NodeId;
	using QtNodes::PortType;

	const auto c_DataRoot = std::filesystem::path("C:/proj/Data");

	// Outlives every registry and node built below, several of which hold it by reference.
	const editor::LanguageResolver c_Language;

	/**
	 * A surface as registration would reflect it: two values with their declared defaults, a colour
	 * slot, a coverage slot and a data slot. Hand-built, which is exactly what the registry accepts
	 * a span of -- no device, no .slang.
	 */
	bgl::SurfaceType
	RimSurface()
	{
		auto surface        = bgl::SurfaceType();
		surface.surfaceName = "Rim";

		auto power         = bgl::SurfaceValue();
		power.name         = "rimPower";
		power.type         = bgl::SurfaceValueType::kFloat;
		power.defaultValue = glm::vec4(8.0f, 0.0f, 0.0f, 0.0f);

		auto colour         = bgl::SurfaceValue();
		colour.name         = "rimColor";
		colour.type         = bgl::SurfaceValueType::kFloat3;
		colour.defaultValue = glm::vec4(1.0f, 0.5f, 0.25f, 0.0f);
		colour.isColor      = true;

		auto base = bgl::SurfaceTexture();
		base.name = "baseColor";
		base.kind = bgl::SurfaceTextureKind::kColor;

		auto mask  = bgl::SurfaceTexture();
		mask.name  = "mask";
		mask.kind  = bgl::SurfaceTextureKind::kCoverage;
		mask.index = 1;

		auto orm  = bgl::SurfaceTexture();
		orm.name  = "orm";
		orm.kind  = bgl::SurfaceTextureKind::kData;
		orm.index = 2;

		surface.params.values   = { power, colour };
		surface.params.textures = { base, mask, orm };
		return surface;
	}

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry>
	Registry()
	{
		static const bgl::SurfaceType c_Surface = RimSurface();
		return MakeMaterialNodeRegistry(c_Language, nullptr, nullptr, { &c_Surface, 1 });
	}

	SurfaceOutputNode*
	Sink(MaterialGraphModel& model)
	{
		return qobject_cast<SurfaceOutputNode*>(model.OutputNode());
	}
}

TEST_CASE("A surface sink is its surface, reflected", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId id = model.addNode(QStringLiteral("SurfaceOutput:Rim"));
	REQUIRE(id != InvalidNodeId);

	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);

	// One whole-texture port per slot, a data slot's included until it is split; nothing flows
	// out of a sink.
	CHECK(sink->nPorts(PortType::In) == 3u);
	CHECK(sink->nPorts(PortType::Out) == 0u);
	CHECK(sink->portCaption(PortType::In, 0) == QStringLiteral("baseColor (Color)"));
	CHECK(sink->portCaption(PortType::In, 1) == QStringLiteral("mask (Coverage)"));
	CHECK(sink->portCaption(PortType::In, 2) == QStringLiteral("orm (Data)"));
	CHECK(sink->dataType(PortType::In, 0).id == QStringLiteral("surfacetexture"));
	CHECK(sink->dataType(PortType::In, 2).id == QStringLiteral("surfacetexture"));
	CHECK_FALSE(sink->IsSplit(2));
	CHECK(sink->WholePortFor(2) == 2u);
	CHECK(sink->ChannelPortFor(2, 3) == 2u);

	// The declaration's defaults arrive prefilled, components past the type's staying zero.
	CHECK(sink->Value(0).x == 8.0f);
	CHECK(sink->Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));

	// Found, guarded and switched exactly as the PBR sinks are.
	CHECK(model.OutputNodeId() == id);
	CHECK_FALSE(model.deleteNode(id));
}

TEST_CASE("Splitting a data slot swaps its whole port for channels", "[materialgraph][surfacesink]")
{
	SurfaceOutputNode sink(c_Language, RimSurface());

	sink.SetSplit(2, true);
	CHECK(sink.IsSplit(2));
	CHECK(sink.nPorts(PortType::In) == 6u);
	CHECK(sink.portCaption(PortType::In, 2) == QStringLiteral("orm.r"));
	CHECK(sink.portCaption(PortType::In, 5) == QStringLiteral("orm.a"));
	CHECK(sink.dataType(PortType::In, 2).id != QStringLiteral("surfacetexture"));
	CHECK(sink.WholePortFor(2) == 2u);
	CHECK(sink.ChannelPortFor(2, 0) == 2u);
	CHECK(sink.ChannelPortFor(2, 3) == 5u);

	// Only a data slot splits: a colour or coverage map is authored whole.
	sink.SetSplit(0, true);
	sink.SetSplit(1, true);
	CHECK_FALSE(sink.IsSplit(0));
	CHECK_FALSE(sink.IsSplit(1));

	sink.SetSplit(2, false);
	CHECK(sink.nPorts(PortType::In) == 3u);
	CHECK(sink.portCaption(PortType::In, 2) == QStringLiteral("orm (Data)"));
}

TEST_CASE("A sink round-trips its split slots by name", "[materialgraph][surfacesink]")
{
	SurfaceOutputNode saved(c_Language, RimSurface());
	saved.SetSplit(2, true);
	CHECK(saved.save()["split"].toObject()["orm"].toBool());

	SurfaceOutputNode reloaded(c_Language, RimSurface());
	reloaded.load(saved.save());
	CHECK(reloaded.IsSplit(2));
	CHECK(reloaded.nPorts(PortType::In) == 6u);

	// Keyed by name: a surface that declares its slots in another order splits the same slot.
	bgl::SurfaceType reordered = RimSurface();
	std::swap(reordered.params.textures[0], reordered.params.textures[2]);
	SurfaceOutputNode moved(c_Language, reordered);
	moved.load(saved.save());
	CHECK(moved.IsSplit(0));
	CHECK(moved.portCaption(PortType::In, 0) == QStringLiteral("orm.r"));
}

TEST_CASE("A routed channel cannot wire into a whole-texture port", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode(QStringLiteral("Texture"));
	const NodeId sink    = model.addNode(QStringLiteral("SurfaceOutput:Rim"));

	// A whole-texture port is bound, not composited: every channel port -- bundle or scalar -- is
	// refused by type there, and only the whole-texture output fits.
	for (QtNodes::PortIndex port = 0; port < QtNodes::PortIndex(TextureNode::c_TexturePort); ++port)
	{
		INFO("texture port " << port);
		CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, port, sink, 0 }));
	}

	CHECK(model.connectionPossible(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sink, 0 }));
}

TEST_CASE(
	"A data slot routes single channels once split, and its wires go with its ports",
	"[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId       texture = model.addNode(QStringLiteral("Texture"));
	const NodeId       sinkId  = model.addNode(QStringLiteral("SurfaceOutput:Rim"));
	SurfaceOutputNode* sink    = Sink(model);
	REQUIRE(sink != nullptr);

	if (auto* node = model.delegateModel<TextureNode>(texture))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));

	constexpr auto c_TextureR     = QtNodes::PortIndex(TextureNode::c_BundleCount);
	constexpr auto c_TextureWhole = QtNodes::PortIndex(TextureNode::c_TexturePort);

	// Whole: the slot's one port takes the texture, and no single channel.
	const auto ormWhole = QtNodes::PortIndex(sink->WholePortFor(2));
	CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, c_TextureR, sinkId, ormWhole }));
	model.addConnection(ConnectionId{ texture, c_TextureWhole, sinkId, ormWhole });
	CHECK(sink->BoundTexture(2).endsWith(QStringLiteral("ao.ktx2")));

	// Splitting replaces the port, so the whole wire goes with it -- as splitting a PBR group does.
	sink->SetSplit(2, true);
	CHECK(model.allConnectionIds(sinkId).empty());
	CHECK(sink->BoundTexture(2).isEmpty());

	// A channel port takes a single channel and nothing wider -- a bundle or the whole texture
	// would silently drop the swizzle it carries.
	const auto ormR = QtNodes::PortIndex(sink->ChannelPortFor(2, 0));
	const auto ormG = QtNodes::PortIndex(sink->ChannelPortFor(2, 1));
	CHECK(model.connectionPossible(ConnectionId{ texture, c_TextureR, sinkId, ormR }));
	CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, 0, sinkId, ormR }));
	CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, c_TextureWhole, sinkId, ormR }));

	model.addConnection(ConnectionId{ texture, c_TextureR, sinkId, ormR });
	model.addConnection(ConnectionId{ texture, c_TextureR + 1, sinkId, ormG });
	CHECK(sink->SlotIsRouted(2));

	// Unsplitting drops the channel wires the same way, leaving the slot unbound.
	sink->SetSplit(2, false);
	CHECK(model.allConnectionIds(sinkId).empty());
	CHECK_FALSE(sink->SlotIsRouted(2));
	CHECK(model.connectionPossible(ConnectionId{ texture, c_TextureWhole, sinkId, ormWhole }));
}

TEST_CASE("A routed slot compiles to its routes, not a binding", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId       ao     = model.addNode(QStringLiteral("Texture"));
	const NodeId       mr     = model.addNode(QStringLiteral("Texture"));
	const NodeId       sinkId = model.addNode(QStringLiteral("SurfaceOutput:Rim"));
	SurfaceOutputNode* sink   = Sink(model);
	REQUIRE(sink != nullptr);

	if (auto* node = model.delegateModel<TextureNode>(ao))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));
	if (auto* node = model.delegateModel<TextureNode>(mr))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/mr.ktx2"));

	sink->SetSplit(2, true);

	// The angelica shape: AO from one texture's R, roughness/metallic from another's G and B.
	constexpr auto c_R = QtNodes::PortIndex(TextureNode::c_BundleCount);
	model.addConnection(
		ConnectionId{ ao, c_R, sinkId, QtNodes::PortIndex(sink->ChannelPortFor(2, 0)) });
	model.addConnection(
		ConnectionId{ mr, c_R + 1, sinkId, QtNodes::PortIndex(sink->ChannelPortFor(2, 1)) });
	model.addConnection(
		ConnectionId{ mr, c_R + 2, sinkId, QtNodes::PortIndex(sink->ChannelPortFor(2, 2)) });

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("head_rim"), c_DataRoot);

	REQUIRE(material.surface.textures.size() == 1);
	const assetlib::SurfaceTextureBinding& orm = material.surface.textures[0];
	CHECK(orm.name == "orm");
	CHECK(orm.texturePath.empty());
	CHECK(orm.routes[0].texture == "Derived/SourceTextures/head/ao.ktx2");
	CHECK(orm.routes[0].channel == 0);
	CHECK(orm.routes[1].texture == "Derived/SourceTextures/head/mr.ktx2");
	CHECK(orm.routes[1].channel == 1);
	CHECK(orm.routes[2].texture == "Derived/SourceTextures/head/mr.ktx2");
	CHECK(orm.routes[2].channel == 2);
	CHECK(orm.routes[3].texture.empty());
}

TEST_CASE("A routed document builds its board", "[materialgraph][surfacesink]")
{
	auto material                = assetlib::BMaterial();
	material.shadingModel        = assetlib::ShadingModel::kPbrSurface;
	material.surface.surfaceName = "Rim";

	auto& orm     = material.surface.textures.emplace_back();
	orm.name      = "orm";
	orm.routes[0] = { "Derived/SourceTextures/head/ao.ktx2", 0 };
	orm.routes[1] = { "Derived/SourceTextures/head/mr.ktx2", 1 };
	orm.routes[2] = { "Derived/SourceTextures/head/mr.ktx2", 2 };

	MaterialGraphModel model(Registry());
	REQUIRE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));

	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);

	// A slot the document routes opens split, so its routes have their ports to land on.
	CHECK(sink->IsSplit(2));
	REQUIRE(sink->SlotIsRouted(2));

	// Absolute like every live-graph path, per the whole-binding case above.
	CHECK(
		sink->RouteFor(2, 0).path.endsWith(QStringLiteral("Derived/SourceTextures/head/ao.ktx2")));
	CHECK(sink->RouteFor(2, 0).channel == 0);
	CHECK(
		sink->RouteFor(2, 1).path.endsWith(QStringLiteral("Derived/SourceTextures/head/mr.ktx2")));
	CHECK(sink->RouteFor(2, 1).channel == 1);
	CHECK(sink->RouteFor(2, 2).channel == 2);
	CHECK(sink->RouteFor(2, 3).path.isEmpty());

	// One texture node per distinct source, not per route: mr feeds two channels through one node.
	auto textureNodes = 0;
	for (const NodeId id : model.allNodeIds())
		if (model.delegateModel<TextureNode>(id) != nullptr)
			++textureNodes;
	CHECK(textureNodes == 2);

	// And back out: the board compiles to the document it was built from.
	const assetlib::BMaterial back = CompileMaterial(model, QStringLiteral("rim"), c_DataRoot);
	REQUIRE(back.surface.textures.size() == 1);
	CHECK(back.surface.textures[0].routes[0].texture == "Derived/SourceTextures/head/ao.ktx2");
	CHECK(back.surface.textures[0].routes[1].texture == "Derived/SourceTextures/head/mr.ktx2");
}

TEST_CASE("The whole-texture port binds a slot", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode(QStringLiteral("Texture"));
	const NodeId sink    = model.addNode(QStringLiteral("SurfaceOutput:Rim"));

	if (auto* node = model.delegateModel<TextureNode>(texture))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/rim.ktx2"));

	model.addConnection(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sink, 0 });

	REQUIRE(Sink(model) != nullptr);
	CHECK(
		Sink(model)->BoundTexture(0) ==
		QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/rim.ktx2"));
	CHECK(Sink(model)->BoundTexture(1).isEmpty());
}

TEST_CASE("A surface board compiles to the surface document", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode(QStringLiteral("Texture"));
	const NodeId sink    = model.addNode(QStringLiteral("SurfaceOutput:Rim"));

	if (auto* node = model.delegateModel<TextureNode>(texture))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/rim.ktx2"));
	model.addConnection(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sink, 0 });

	auto edited          = QJsonObject();
	edited["parameters"] = QJsonObject{ { "rimPower", QJsonArray{ 2.5 } } };
	Sink(model)->load(edited);

	const assetlib::BMaterial material =
		CompileMaterial(model, QStringLiteral("head_rim"), c_DataRoot);

	CHECK(material.shadingModel == assetlib::ShadingModel::kPbrSurface);
	CHECK(material.surface.surfaceName == "Rim");

	// Every declared value is written at its declared width -- an edited one at its edit, an
	// untouched one at its default -- so the document says what the panel showed.
	REQUIRE(material.surface.values.size() == 2);
	CHECK(material.surface.values[0].name == "rimPower");
	REQUIRE(material.surface.values[0].value.size() == 1);
	CHECK(material.surface.values[0].value[0] == Catch::Approx(2.5f));
	CHECK(material.surface.values[1].name == "rimColor");
	REQUIRE(material.surface.values[1].value.size() == 3);
	CHECK(material.surface.values[1].value[1] == Catch::Approx(0.5f));

	// A bound slot is written data-root-relative; an unbound one is simply absent, and the
	// surface samples its default.
	REQUIRE(material.surface.textures.size() == 1);
	CHECK(material.surface.textures[0].name == "baseColor");
	CHECK(material.surface.textures[0].texturePath == "Derived/SourceTextures/head/rim.ktx2");

	// The layer keys are every model's, and the sink authors them.
	CHECK(material.layer.alphaMode == assetlib::AlphaMode::kOpaque);
	CHECK(material.layer.doubleSided);

	CHECK_FALSE(material.editorGraph.empty());
}

TEST_CASE("A surface sink round-trips through save and load", "[materialgraph][surfacesink]")
{
	SurfaceOutputNode saved(c_Language, RimSurface());

	auto edited           = QJsonObject();
	edited["parameters"]  = QJsonObject{ { "rimColor", QJsonArray{ 0.1, 0.2, 0.3 } } };
	edited["alphaMode"]   = QStringLiteral("hashed");
	edited["doubleSided"] = false;
	saved.load(edited);

	SurfaceOutputNode reloaded(c_Language, RimSurface());
	reloaded.load(saved.save());

	CHECK(reloaded.Value(1) == glm::vec4(0.1f, 0.2f, 0.3f, 0.0f));
	CHECK(reloaded.Value(0).x == 8.0f);
	CHECK(reloaded.GetAlphaMode() == assetlib::AlphaMode::kHashed);

	auto compiled = assetlib::BMaterial();
	reloaded.CompileInto(compiled, c_DataRoot);
	CHECK(compiled.layer.alphaMode == assetlib::AlphaMode::kHashed);
	CHECK_FALSE(compiled.layer.doubleSided);
}

TEST_CASE("A graph with no rim key loads the rim at its default", "[materialgraph][surfacesink]")
{
	// A graph saved before a value was added to the surface carries no key for it, and must load
	// as the declaration's default rather than as zero.
	SurfaceOutputNode node(c_Language, RimSurface());

	node.load(QJsonObject{ { "parameters", QJsonObject{} } });

	CHECK(node.Value(0).x == 8.0f);
	CHECK(node.Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
}

TEST_CASE("A surface document builds its board", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	auto material                = assetlib::BMaterial();
	material.name                = "head_rim";
	material.shadingModel        = assetlib::ShadingModel::kPbrSurface;
	material.layer.alphaMode     = assetlib::AlphaMode::kHashed;
	material.layer.doubleSided   = false;
	material.surface.surfaceName = "Rim";
	material.surface.values      = { { "rimPower", { 2.5f } } };
	material.surface.textures    = { { "baseColor", "Derived/SourceTextures/head/rim.ktx2" } };

	REQUIRE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));

	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);

	// The document's edits arrive; a value it does not set stays at the declaration's default.
	CHECK(sink->Value(0).x == 2.5f);
	CHECK(sink->Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
	CHECK(sink->GetAlphaMode() == assetlib::AlphaMode::kHashed);

	// The bound slot holds the file, absolute like every live-graph path.
	CHECK(sink->BoundTexture(0).endsWith(QStringLiteral("Derived/SourceTextures/head/rim.ktx2")));
	CHECK(sink->BoundTexture(1).isEmpty());

	// And back out: the board compiles to the document that built it.
	const assetlib::BMaterial compiled =
		CompileMaterial(model, QStringLiteral("head_rim"), c_DataRoot);
	CHECK(compiled.shadingModel == assetlib::ShadingModel::kPbrSurface);
	CHECK(compiled.layer.alphaMode == assetlib::AlphaMode::kHashed);
	CHECK_FALSE(compiled.layer.doubleSided);
	REQUIRE(compiled.surface.textures.size() == 1u);
	CHECK(compiled.surface.textures[0].texturePath == "Derived/SourceTextures/head/rim.ktx2");
}

TEST_CASE(
	"A document naming an unregistered surface builds no board",
	"[materialgraph][surfacesink]")
{
	// Registration happens once at startup, so a second project's surface has no sink here. The
	// board is the surface's or nothing -- a PBR fallback would be compiled into a demotion by
	// the next Save.
	MaterialGraphModel model(MakeMaterialNodeRegistry(c_Language, nullptr, nullptr, {}));

	auto material                = assetlib::BMaterial();
	material.shadingModel        = assetlib::ShadingModel::kPbrSurface;
	material.surface.surfaceName = "Rim";

	CHECK_FALSE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));
	CHECK(model.allNodeIds().empty());
}

TEST_CASE("A binding the surface does not declare is skipped", "[materialgraph][surfacesink]")
{
	// The same document is refused at CreateSurfaceMaterial by name; the board simply cannot show
	// the stray binding, and the rest of the material still opens.
	MaterialGraphModel model(Registry());

	auto material                = assetlib::BMaterial();
	material.shadingModel        = assetlib::ShadingModel::kPbrSurface;
	material.surface.surfaceName = "Rim";
	material.surface.textures    = { { "glitter", "Derived/SourceTextures/head/glitter.ktx2" },
		                             { "baseColor", "Derived/SourceTextures/head/rim.ktx2" } };

	REQUIRE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));

	REQUIRE(Sink(model) != nullptr);
	CHECK(Sink(model)->BoundTexture(0).endsWith(QStringLiteral("rim.ktx2")));

	// One sink, one texture node: nothing was placed for the binding that has no slot.
	CHECK(model.allNodeIds().size() == 2u);
}

TEST_CASE("Two slots sharing one file share one texture node", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	auto material                = assetlib::BMaterial();
	material.shadingModel        = assetlib::ShadingModel::kPbrSurface;
	material.surface.surfaceName = "Rim";
	material.surface.textures    = { { "baseColor", "Derived/SourceTextures/head/rim.ktx2" },
		                             { "mask", "Derived/SourceTextures/head/rim.ktx2" } };

	REQUIRE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));

	REQUIRE(Sink(model) != nullptr);
	CHECK(Sink(model)->BoundTexture(0) == Sink(model)->BoundTexture(1));
	CHECK_FALSE(Sink(model)->BoundTexture(0).isEmpty());
	CHECK(model.allNodeIds().size() == 2u);
}

TEST_CASE("A saved board says which sink it holds", "[materialgraph][surfacesink]")
{
	// What OpenMaterialInto branches on: a surface document whose stored graph already holds the
	// surface's sink reloads that board; any other stored graph -- the blank PBR relic every
	// surface document carried while its board could not be authored -- is rebuilt from the
	// document instead.
	MaterialGraphModel surface(Registry());
	surface.addNode(QStringLiteral("SurfaceOutput:Rim"));
	CHECK(GraphHoldsNodeType(surface.save(), QStringLiteral("SurfaceOutput:Rim")));

	MaterialGraphModel pbr(Registry());
	pbr.addNode(QStringLiteral("MaterialOutput"));
	CHECK_FALSE(GraphHoldsNodeType(pbr.save(), QStringLiteral("SurfaceOutput:Rim")));
}

TEST_CASE(
	"Switching a PBR board to a surface keeps its place and drops its wires",
	"[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode(QStringLiteral("Texture"));
	const NodeId opaque  = model.addNode(QStringLiteral("MaterialOutput"));
	model.setNodeData(opaque, QtNodes::NodeRole::Position, QPointF(120.0, -45.0));
	model.addConnection(ConnectionId{ texture, 1, opaque, 0 });

	REQUIRE(model.SetOutputType(QStringLiteral("SurfaceOutput:Rim")));

	const NodeId sink = model.OutputNodeId();
	REQUIRE(sink != InvalidNodeId);
	REQUIRE(Sink(model) != nullptr);

	// The node stays put; the RGB wire has no port whose type still fits, so it goes rather than
	// silently reinterpreting a routed bundle as a bound texture.
	CHECK(
		model.nodeData(sink, QtNodes::NodeRole::Position).value<QPointF>() ==
		QPointF(120.0, -45.0));
	CHECK(model.allConnectionIds(sink).empty());

	// And back: the surface sink is switched away from exactly as it was switched to.
	REQUIRE(model.SetOutputType(QStringLiteral("MaterialOutput")));
	CHECK(Sink(model) == nullptr);
	CHECK(model.OutputNode() != nullptr);
}

TEST_CASE("The node re-measures when its widget resizes", "[materialgraph][surfacesink]")
{
	// QtNodes reads the embedded widget's size only when the node is created, so a widget that
	// settles on first show -- or a form row shown or hidden later -- must ask for a re-measure
	// itself, or its contents overflow the frame.
	SurfaceOutputNode sink(c_Language, RimSurface());
	QWidget*          widget = sink.embeddedWidget();
	REQUIRE(widget != nullptr);

	// A hidden widget defers its resize events; the proxied one in the graph is shown.
	widget->show();

	QSignalSpy remeasured(&sink, &QtNodes::NodeDelegateModel::requestNodeUpdate);
	widget->resize(widget->width() + 40, widget->height() + 25);
	CHECK(remeasured.count() == 1);
}

TEST_CASE("A colour value carries a swatch, and picking writes it", "[materialgraph][surfacesink]")
{
	// A [Color] float4 and an unmarked float3, so the swatch provably follows the flag rather
	// than the width. A marked value is the swatch alone -- no spins beside it -- so the spin
	// count is the unmarked value's. The picker's own dialog is modal and cannot run headless;
	// SetValue is the write it lands, so it is what the case drives.
	auto surface        = bgl::SurfaceType();
	surface.surfaceName = "Swatch";

	auto glow         = bgl::SurfaceValue();
	glow.name         = "glowColor";
	glow.type         = bgl::SurfaceValueType::kFloat4;
	glow.defaultValue = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
	glow.isColor      = true;

	auto offset         = bgl::SurfaceValue();
	offset.name         = "offset";
	offset.type         = bgl::SurfaceValueType::kFloat3;
	offset.defaultValue = glm::vec4(0.0f);

	surface.params.values = { glow, offset };

	SurfaceOutputNode sink(c_Language, surface);
	QWidget*          widget = sink.embeddedWidget();
	REQUIRE(widget != nullptr);

	// One swatch: the marked value's row alone, an unmarked float3 keeps its spins alone.
	const auto swatches = widget->findChildren<QPushButton*>();
	REQUIRE(swatches.size() == 1);
	CHECK(widget->findChildren<QDoubleSpinBox*>().size() == 3);

	// A float4's alpha rides the swatch as text; the swatch itself stays opaque.
	CHECK(swatches[0]->text() == QStringLiteral("A 0.40"));

	QSignalSpy changed(&sink, &MaterialSinkNode::Changed);

	sink.SetValue(0, glm::vec4(0.5f, 0.6f, 0.7f, 0.8f));
	CHECK(sink.Value(0) == glm::vec4(0.5f, 0.6f, 0.7f, 0.8f));
	CHECK(changed.count() == 1);
	CHECK(swatches[0]->text() == QStringLiteral("A 0.80"));

	// Components past the type's are zeroed, so a float3 write cannot smuggle an alpha in --
	// and a write that lands the value already held is not a change.
	sink.SetValue(1, glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
	CHECK(sink.Value(1) == glm::vec4(1.0f, 2.0f, 3.0f, 0.0f));
	CHECK(changed.count() == 2);
	sink.SetValue(1, glm::vec4(1.0f, 2.0f, 3.0f, 9.0f));
	CHECK(changed.count() == 2);
}

TEST_CASE(
	"The layer setters write the state the material compiles from",
	"[materialgraph][surfacesink]")
{
	// The layer keys are authored in the properties panel (ADR-9); these setters are what its
	// widgets write, and each change recompiles the preview exactly as a board edit does.
	SurfaceOutputNode sink(c_Language, RimSurface());
	QSignalSpy        changed(&sink, &MaterialSinkNode::Changed);

	sink.SetAlphaMode(assetlib::AlphaMode::kMask);
	sink.SetAlphaCutoff(0.25f);
	sink.SetDoubleSided(false);
	CHECK(changed.count() == 3);

	// Setting what already holds is not a change, so the preview is not recompiled for it.
	sink.SetAlphaMode(assetlib::AlphaMode::kMask);
	CHECK(changed.count() == 3);

	CHECK(sink.GetAlphaMode() == assetlib::AlphaMode::kMask);
	CHECK(sink.GetAlphaCutoff() == 0.25f);
	CHECK_FALSE(sink.GetDoubleSided());

	assetlib::BMaterial material;
	sink.CompileInto(material, c_DataRoot);
	CHECK(material.layer.alphaMode == assetlib::AlphaMode::kMask);
	CHECK(material.layer.alphaCutoff == 0.25f);
	CHECK_FALSE(material.layer.doubleSided);
}

TEST_CASE("The node carries no layer widgets", "[materialgraph][surfacesink]")
{
	// ADR-9: a combo popup is a child window the proxy embeds unscaled into the zoomed scene, so
	// the layer moved to the panel and the node's widget holds value spins -- and a data slot's
	// split box, Double Sided being the panel's.
	SurfaceOutputNode sink(c_Language, RimSurface());
	QWidget*          widget = sink.embeddedWidget();
	REQUIRE(widget != nullptr);
	CHECK(widget->findChild<QComboBox*>() == nullptr);

	const auto boxes = widget->findChildren<QCheckBox*>();
	REQUIRE(boxes.size() == 1);
	CHECK_FALSE(boxes[0]->isChecked());

	// The box is the split: ticking it splits the slot, and a load that splits it ticks it.
	boxes[0]->setChecked(true);
	CHECK(sink.IsSplit(2));
	sink.load(QJsonObject{ { "split", QJsonObject{ { "orm", false } } } });
	CHECK_FALSE(boxes[0]->isChecked());
	CHECK_FALSE(sink.IsSplit(2));
}

TEST_CASE(
	"A board saved before slots split opens with its channel wires split",
	"[materialgraph][surfacesink]")
{
	// The layout such a board was saved in: every slot's whole port, and the data slot's four
	// channel ports after its own -- baseColor 0, mask 1, orm 2, orm.r..a 3..6.
	MaterialGraphModel saved(Registry());
	const NodeId       ao     = saved.addNode(QStringLiteral("Texture"));
	const NodeId       base   = saved.addNode(QStringLiteral("Texture"));
	const NodeId       sinkId = saved.addNode(QStringLiteral("SurfaceOutput:Rim"));
	if (auto* node = saved.delegateModel<TextureNode>(ao))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));
	if (auto* node = saved.delegateModel<TextureNode>(base))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/base.ktx2"));

	constexpr auto c_TextureR     = TextureNode::c_BundleCount;
	constexpr auto c_TextureWhole = TextureNode::c_TexturePort;
	const auto     wire           = [](NodeId out, unsigned int outPort, NodeId in, int inPort) {
		return QJsonObject{ { "outNodeId", static_cast<int>(out) },
			                { "outPortIndex", static_cast<int>(outPort) },
			                { "inNodeId", static_cast<int>(in) },
			                { "inPortIndex", inPort } };
	};

	QJsonObject graph = saved.save();
	QJsonArray  nodes = graph["nodes"].toArray();
	for (QJsonValueRef value : nodes)
	{
		QJsonObject node     = value.toObject();
		QJsonObject internal = node["internal-data"].toObject();
		internal.remove(QStringLiteral("split"));
		node["internal-data"] = internal;
		value                 = node;
	}
	graph["nodes"]       = nodes;
	graph["connections"] = QJsonArray{ wire(base, c_TextureWhole, sinkId, 0),
		                               wire(ao, c_TextureR, sinkId, 3),
		                               wire(ao, c_TextureR + 2, sinkId, 5) };

	UpgradeSurfaceSinkPorts(graph, *Registry());

	MaterialGraphModel model(Registry());
	model.load(graph);
	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);

	CHECK(sink->IsSplit(2));
	CHECK(sink->BoundTexture(0).endsWith(QStringLiteral("base.ktx2")));
	CHECK(sink->RouteFor(2, 0).path.endsWith(QStringLiteral("ao.ktx2")));
	CHECK(sink->RouteFor(2, 0).channel == 0);
	CHECK(sink->RouteFor(2, 2).channel == 2);
	CHECK(sink->RouteFor(2, 1).path.isEmpty());
	CHECK(model.allConnectionIds(model.OutputNodeId()).size() == 3u);

	// And a board of that age with no channel wire opens with the slot whole.
	graph["nodes"]       = nodes;
	graph["connections"] = QJsonArray{ wire(base, c_TextureWhole, sinkId, 2) };
	UpgradeSurfaceSinkPorts(graph, *Registry());

	MaterialGraphModel whole(Registry());
	whole.load(graph);
	REQUIRE(Sink(whole) != nullptr);
	CHECK_FALSE(Sink(whole)->IsSplit(2));
	CHECK(Sink(whole)->BoundTexture(2).endsWith(QStringLiteral("base.ktx2")));
}

TEST_CASE("A routed board's desc carries its wires as routes", "[materialgraph][surfacesink]")
{
	// The wires are the routes and their uploads are the handles: nothing is composited for the
	// preview, so a rewire costs the desc rebuild and the record write behind it, nothing more.
	MaterialGraphModel model(Registry());

	const NodeId       ao     = model.addNode(QStringLiteral("Texture"));
	const NodeId       mr     = model.addNode(QStringLiteral("Texture"));
	const NodeId       sinkId = model.addNode(QStringLiteral("SurfaceOutput:Rim"));
	SurfaceOutputNode* sink   = Sink(model);
	REQUIRE(sink != nullptr);

	if (auto* node = model.delegateModel<TextureNode>(ao))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));
	if (auto* node = model.delegateModel<TextureNode>(mr))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/mr.ktx2"));

	sink->SetSplit(2, true);

	constexpr auto c_TextureR = QtNodes::PortIndex(TextureNode::c_BundleCount);
	const auto     ormR       = QtNodes::PortIndex(sink->ChannelPortFor(2, 0));
	model.addConnection(ConnectionId{ ao, c_TextureR, sinkId, ormR });
	model.addConnection(
		ConnectionId{ mr, c_TextureR + 1, sinkId, QtNodes::PortIndex(sink->ChannelPortFor(2, 1)) });

	const bgl::SurfaceMaterialDesc desc = editor::SurfaceDescOfBoard(*sink);

	// The routed slot rides as routes, not a binding; with no device the handles are null and
	// the channels still say which component each wire feeds.
	REQUIRE(desc.textures.size() == 1);
	const bgl::SurfaceTextureBinding& orm = desc.textures[0];
	CHECK(orm.name == "orm");
	CHECK(orm.texture.textureSlot.is_null());
	CHECK(orm.routes[0].channel == 0);
	CHECK(orm.routes[1].channel == 1);
	CHECK(orm.routes[2].texture.textureSlot.is_null());
	CHECK(orm.routes[3].texture.textureSlot.is_null());

	// A rewire moves the desc with it: the same component fed from another channel.
	model.deleteConnection(ConnectionId{ ao, c_TextureR, sinkId, ormR });
	model.addConnection(ConnectionId{ ao, c_TextureR + 2, sinkId, ormR });

	const bgl::SurfaceMaterialDesc rewired = editor::SurfaceDescOfBoard(*sink);
	REQUIRE(rewired.textures.size() == 1);
	CHECK(rewired.textures[0].routes[0].channel == 2);
}

TEST_CASE(
	"A surface's sink shows no Geometry Occlusion (UV1) port",
	"[materialgraph][surfacesink][geometryao]")
{
	// A surface takes geometry AO the way it takes any map -- through a slot it declares and
	// samples itself -- so its node shows the slots it declares and no port it never names.
	MaterialGraphModel model(Registry());
	REQUIRE(model.addNode(QStringLiteral("SurfaceOutput:Rim")) != InvalidNodeId);

	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);

	for (unsigned int port = 0; port < sink->nPorts(PortType::In); ++port)
	{
		INFO("port " << port);
		CHECK(
			sink->portCaption(PortType::In, static_cast<QtNodes::PortIndex>(port)) !=
			QStringLiteral("Geometry Occlusion (UV1)"));
	}
}

TEST_CASE(
	"Switching a PBR board to a surface lets its geometry occlusion wire go",
	"[materialgraph][surfacesink][geometryao]")
{
	// The opaque sink's geometry occlusion port sits at the index of Rim's orm.r, a single-channel port the wire's
	// type fits: moved by index, the occlusion map would be bound as the surface's ORM red.
	MaterialGraphModel model(Registry());
	const NodeId       outputId  = model.addNode(QStringLiteral("MaterialOutput"));
	const NodeId       textureId = model.addNode(QStringLiteral("Texture"));
	if (auto* texture = model.delegateModel<TextureNode>(textureId))
		texture->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/wall/ao.ktx2"));

	const auto* pbr = qobject_cast<const MaterialOutputNode*>(model.OutputNode());
	REQUIRE(pbr != nullptr);
	model.addConnection(
		ConnectionId{ textureId,
	                  QtNodes::PortIndex(TextureNode::c_BundleCount),
	                  outputId,
	                  pbr->GeometryOcclusionPort() });

	REQUIRE(model.SetOutputType(QStringLiteral("SurfaceOutput:Rim")));

	CHECK(model.allConnectionIds(model.OutputNodeId()).empty());
}
