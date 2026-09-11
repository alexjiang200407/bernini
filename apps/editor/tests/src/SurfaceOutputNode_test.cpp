#include "Windows/MaterialEditor/MaterialGraphModel.h"
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
#include <bgl/glm.h>
#include <filesystem>
#include <memory>
#include <qjsonobject.h>
#include <qobject.h>
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
	 * slot and a coverage slot. Hand-built, which is exactly what the registry accepts a span of --
	 * no device, no .slang.
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

		surface.params.values   = { power, colour };
		surface.params.textures = { base, mask };
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

	// One port per texture slot, captioned by name and kind; nothing flows out of a sink.
	CHECK(sink->nPorts(PortType::In) == 2u);
	CHECK(sink->nPorts(PortType::Out) == 0u);
	CHECK(sink->portCaption(PortType::In, 0) == QStringLiteral("baseColor (Color)"));
	CHECK(sink->portCaption(PortType::In, 1) == QStringLiteral("mask (Coverage)"));
	CHECK(sink->dataType(PortType::In, 0).id == QStringLiteral("surfacetexture"));

	// The declaration's defaults arrive prefilled, components past the type's staying zero.
	CHECK(sink->Value(0).x == 8.0f);
	CHECK(sink->Value(1) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));

	// Found, guarded and switched exactly as the PBR sinks are.
	CHECK(model.OutputNodeId() == id);
	CHECK_FALSE(model.deleteNode(id));
}

TEST_CASE("A routed channel cannot wire into a surface slot", "[materialgraph][surfacesink]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode(QStringLiteral("Texture"));
	const NodeId sink    = model.addNode(QStringLiteral("SurfaceOutput:Rim"));

	// A surface texture is bound, not composited: every channel port -- bundle or scalar -- is
	// refused by type, and only the whole-texture port fits. The rule the contract states is
	// enforced by the wire.
	for (QtNodes::PortIndex port = 0; port < QtNodes::PortIndex(TextureNode::c_TexturePort); ++port)
	{
		INFO("texture port " << port);
		CHECK_FALSE(model.connectionPossible(ConnectionId{ texture, port, sink, 0 }));
	}

	CHECK(model.connectionPossible(
		ConnectionId{ texture, QtNodes::PortIndex(TextureNode::c_TexturePort), sink, 0 }));
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
	CHECK(material.surface.textures[0].texture == "Derived/SourceTextures/head/rim.ktx2");

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
	CHECK(compiled.surface.textures[0].texture == "Derived/SourceTextures/head/rim.ktx2");
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
