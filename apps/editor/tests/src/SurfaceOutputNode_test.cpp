#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphScene.h"  // IWYU pragma: keep -- Graph's unique_ptr deletes it
#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/graph_compiler.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>

#include "util/QtSupport.h"  // IWYU pragma: keep
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QSignalSpy>
#include <QWidget>
#include <QtNodes/NodeDelegateModelRegistry>
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>
#include <filesystem>
#include <memory>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringliteral.h>

namespace
{
	using QtNodes::ConnectionId;
	using QtNodes::InvalidNodeId;
	using QtNodes::NodeId;
	using QtNodes::PortType;

	const auto c_DataRoot = std::filesystem::path("C:/proj/Data");

	/**
	 * A surface as registration would reflect it: two values with their declared defaults, a colour
	 * slot, a coverage slot and a data slot. Hand-built, which is exactly what the registry accepts
	 * a span of -- no device, no .slang.
	 */
	bgl::SurfaceType
	RimSurface()
	{
		auto surface = bgl::SurfaceType();
		surface.name = "Rim";

		auto power         = bgl::SurfaceValue();
		power.name         = "rimPower";
		power.type         = bgl::SurfaceValueType::kFloat;
		power.defaultValue = glm::vec4(8.0f, 0.0f, 0.0f, 0.0f);

		auto colour         = bgl::SurfaceValue();
		colour.name         = "rimColor";
		colour.type         = bgl::SurfaceValueType::kFloat3;
		colour.defaultValue = glm::vec4(1.0f, 0.5f, 0.25f, 0.0f);

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
		return MakeMaterialNodeRegistry(nullptr, nullptr, { &c_Surface, 1 });
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

	// A whole-texture port per slot, and a data slot adds one channel port per component
	// (ADR-7); nothing flows out of a sink.
	CHECK(sink->nPorts(PortType::In) == 7u);
	CHECK(sink->nPorts(PortType::Out) == 0u);
	CHECK(sink->portCaption(PortType::In, 0) == QStringLiteral("baseColor (Color)"));
	CHECK(sink->portCaption(PortType::In, 1) == QStringLiteral("mask (Coverage)"));
	CHECK(sink->portCaption(PortType::In, 2) == QStringLiteral("orm (Data)"));
	CHECK(sink->portCaption(PortType::In, 3) == QStringLiteral("orm.r"));
	CHECK(sink->portCaption(PortType::In, 6) == QStringLiteral("orm.a"));
	CHECK(sink->dataType(PortType::In, 0).id == QStringLiteral("surfacetexture"));
	CHECK(sink->dataType(PortType::In, 2).id == QStringLiteral("surfacetexture"));
	CHECK(sink->dataType(PortType::In, 3).id != QStringLiteral("surfacetexture"));
	CHECK(sink->WholePortFor(2) == 2u);
	CHECK(sink->ChannelPortFor(2, 0) == 3u);

	// The declaration's defaults arrive prefilled, components past the type's staying zero.
	CHECK(sink->Value(0).x == 8.0f);
	CHECK(sink->Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));

	// Found, guarded and switched exactly as the PBR sinks are.
	CHECK(model.OutputNodeId() == id);
	CHECK_FALSE(model.deleteNode(id));
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
	"A data slot routes single channels, exclusively with its whole port",
	"[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId       texture = model.addNode(QStringLiteral("Texture"));
	const NodeId       sinkId  = model.addNode(QStringLiteral("SurfaceOutput:Rim"));
	SurfaceOutputNode* sink    = Sink(model);
	REQUIRE(sink != nullptr);

	const auto ormWhole = QtNodes::PortIndex(sink->WholePortFor(2));
	const auto ormR     = QtNodes::PortIndex(sink->ChannelPortFor(2, 0));
	const auto ormG     = QtNodes::PortIndex(sink->ChannelPortFor(2, 1));

	// A channel port takes a single channel and nothing wider -- a bundle or the whole texture
	// would silently drop the swizzle it carries.
	constexpr auto c_TextureR = QtNodes::PortIndex(TextureNode::c_BundleCount);
	CHECK(model.connectionPossible(ConnectionId{ texture, c_TextureR, sinkId, ormR }));
	CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, 0, sinkId, ormR }));
	CHECK_FALSE(model.connectionPossible(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sinkId, ormR }));

	if (auto* node = model.delegateModel<TextureNode>(texture))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));

	// Routed and whole are one slot's two mutually exclusive forms (ADR-7): wiring a channel
	// closes the whole port, and unwiring it opens the port again.
	model.addConnection(ConnectionId{ texture, c_TextureR, sinkId, ormR });
	CHECK(sink->SlotIsRouted(2));
	CHECK_FALSE(model.connectionPossible(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sinkId, ormWhole }));
	CHECK(model.connectionPossible(ConnectionId{ texture, c_TextureR + 1, sinkId, ormG }));

	model.deleteConnection(ConnectionId{ texture, c_TextureR, sinkId, ormR });
	CHECK_FALSE(sink->SlotIsRouted(2));
	CHECK(model.connectionPossible(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sinkId, ormWhole }));

	// And the reverse: a whole binding closes the channel ports.
	model.addConnection(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sinkId, ormWhole });
	CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, c_TextureR, sinkId, ormR }));
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
	auto material         = assetlib::BMaterial();
	material.shadingModel = assetlib::ShadingModel::kPbrSurface;
	material.surface.name = "Rim";

	auto& orm     = material.surface.textures.emplace_back();
	orm.name      = "orm";
	orm.routes[0] = { "Derived/SourceTextures/head/ao.ktx2", 0 };
	orm.routes[1] = { "Derived/SourceTextures/head/mr.ktx2", 1 };
	orm.routes[2] = { "Derived/SourceTextures/head/mr.ktx2", 2 };

	MaterialGraphModel model(Registry());
	REQUIRE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));

	SurfaceOutputNode* sink = Sink(model);
	REQUIRE(sink != nullptr);
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
	CHECK(material.surface.name == "Rim");

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
	SurfaceOutputNode saved(RimSurface());

	auto edited           = QJsonObject();
	edited["parameters"]  = QJsonObject{ { "rimColor", QJsonArray{ 0.1, 0.2, 0.3 } } };
	edited["alphaMode"]   = QStringLiteral("hashed");
	edited["doubleSided"] = false;
	saved.load(edited);

	SurfaceOutputNode reloaded(RimSurface());
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
	SurfaceOutputNode node(RimSurface());

	node.load(QJsonObject{ { "parameters", QJsonObject{} } });

	CHECK(node.Value(0).x == 8.0f);
	CHECK(node.Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
}

TEST_CASE("A surface document builds its board", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	auto material              = assetlib::BMaterial();
	material.name              = "head_rim";
	material.shadingModel      = assetlib::ShadingModel::kPbrSurface;
	material.layer.alphaMode   = assetlib::AlphaMode::kHashed;
	material.layer.doubleSided = false;
	material.surface.name      = "Rim";
	material.surface.values    = { { "rimPower", { 2.5f } } };
	material.surface.textures  = { { "baseColor", "Derived/SourceTextures/head/rim.ktx2" } };

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
	MaterialGraphModel model(MakeMaterialNodeRegistry(nullptr, nullptr, {}));

	auto material         = assetlib::BMaterial();
	material.shadingModel = assetlib::ShadingModel::kPbrSurface;
	material.surface.name = "Rim";

	CHECK_FALSE(BuildSurfaceMaterialGraph(model, material, c_DataRoot));
	CHECK(model.allNodeIds().empty());
}

TEST_CASE("A binding the surface does not declare is skipped", "[materialgraph][surfacesink]")
{
	// The same document is refused at CreateSurfaceMaterial by name; the board simply cannot show
	// the stray binding, and the rest of the material still opens.
	MaterialGraphModel model(Registry());

	auto material             = assetlib::BMaterial();
	material.shadingModel     = assetlib::ShadingModel::kPbrSurface;
	material.surface.name     = "Rim";
	material.surface.textures = { { "glitter", "Derived/SourceTextures/head/glitter.ktx2" },
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

	auto material             = assetlib::BMaterial();
	material.shadingModel     = assetlib::ShadingModel::kPbrSurface;
	material.surface.name     = "Rim";
	material.surface.textures = { { "baseColor", "Derived/SourceTextures/head/rim.ktx2" },
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
	SurfaceOutputNode sink(RimSurface());
	QWidget*          widget = sink.embeddedWidget();
	REQUIRE(widget != nullptr);

	// A hidden widget defers its resize events; the proxied one in the graph is shown.
	widget->show();

	QSignalSpy remeasured(&sink, &QtNodes::NodeDelegateModel::requestNodeUpdate);
	widget->resize(widget->width() + 40, widget->height() + 25);
	CHECK(remeasured.count() == 1);
}

TEST_CASE(
	"The layer setters write the state the material compiles from",
	"[materialgraph][surfacesink]")
{
	// The layer keys are authored in the properties panel (ADR-9); these setters are what its
	// widgets write, and each change recompiles the preview exactly as a board edit does.
	SurfaceOutputNode sink(RimSurface());
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
	// the layer moved to the panel and the node's widget holds value spins alone.
	SurfaceOutputNode sink(RimSurface());
	QWidget*          widget = sink.embeddedWidget();
	REQUIRE(widget != nullptr);
	CHECK(widget->findChild<QComboBox*>() == nullptr);
	CHECK(widget->findChild<QCheckBox*>() == nullptr);
}

TEST_CASE(
	"A delivered compose fills only the slot that still routes it",
	"[materialgraph][surfacesink]")
{
	// The async half of ADR-8's editor side: the compose the compile queued lands later, and by
	// then the board may have moved on. PendingComposedSlot is the rule that decides -- the
	// window fills the entry it returns and drops the image on null.
	auto graph  = MaterialGraphSet::Graph();
	graph.model = std::make_unique<MaterialGraphModel>(Registry());

	const NodeId       ao     = graph.model->addNode(QStringLiteral("Texture"));
	const NodeId       mr     = graph.model->addNode(QStringLiteral("Texture"));
	const NodeId       sinkId = graph.model->addNode(QStringLiteral("SurfaceOutput:Rim"));
	SurfaceOutputNode* sink   = Sink(*graph.model);
	REQUIRE(sink != nullptr);

	if (auto* node = graph.model->delegateModel<TextureNode>(ao))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/ao.ktx2"));
	if (auto* node = graph.model->delegateModel<TextureNode>(mr))
		node->SetTexturePath(QStringLiteral("C:/proj/Data/Derived/SourceTextures/head/mr.ktx2"));

	constexpr auto c_TextureR = QtNodes::PortIndex(TextureNode::c_BundleCount);
	const auto     ormR       = QtNodes::PortIndex(sink->ChannelPortFor(2, 0));
	graph.model->addConnection(ConnectionId{ ao, c_TextureR, sinkId, ormR });
	REQUIRE(sink->SlotIsRouted(2));

	// What EnsureComposedSlots leaves behind when it queues the compose.
	const QString key = editor::SlotRouteKey(*sink, 2);
	graph.composed.push_back({ 2, { key, {} } });

	SECTION("the pending entry under the live key is the one to fill")
	{
		CHECK(editor::PendingComposedSlot(graph, 2, key) == &graph.composed.front().second);
	}

	SECTION("a rewire outruns the delivery, and the stale key finds nothing")
	{
		graph.model->deleteConnection(ConnectionId{ ao, c_TextureR, sinkId, ormR });
		graph.model->addConnection(ConnectionId{ mr, c_TextureR, sinkId, ormR });
		REQUIRE(editor::SlotRouteKey(*sink, 2) != key);

		CHECK(editor::PendingComposedSlot(graph, 2, key) == nullptr);
	}

	SECTION("a slot already delivered is not filled twice")
	{
		graph.composed.front().second.handle = bgl::TextureAssetHandle{ { 0, 1 }, 0 };
		CHECK(editor::PendingComposedSlot(graph, 2, key) == nullptr);
	}

	SECTION("a slot no longer routed drops its delivery")
	{
		graph.model->deleteConnection(ConnectionId{ ao, c_TextureR, sinkId, ormR });
		REQUIRE_FALSE(sink->SlotIsRouted(2));

		CHECK(editor::PendingComposedSlot(graph, 2, key) == nullptr);
	}
}
