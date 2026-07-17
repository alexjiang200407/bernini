#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphScene.h"
#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

#include "util/QtSupport.h"

#include <catch2/catch_approx.hpp>

#include <QDragEnterEvent>
#include <QJsonArray>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtNodes/NodeDelegateModelRegistry>

namespace
{
	using QtNodes::ConnectionId;
	using QtNodes::InvalidNodeId;
	using QtNodes::NodeDelegateModelRegistry;
	using QtNodes::NodeId;
	using QtNodes::NodeRole;

	/**
	 * The registry the material editor ships, minus the graphics. TextureNode takes a null scene and a
	 * null preview cache on purpose -- that is what the editor itself passes when it runs without a
	 * device, and it keeps every test in this file off the GPU.
	 */
	std::shared_ptr<NodeDelegateModelRegistry>
	Registry()
	{
		return MakeMaterialNodeRegistry(nullptr, nullptr);
	}

	/** A drop payload carrying one local file, as the content explorer produces. */
	std::unique_ptr<QMimeData>
	UrlDrop(const QString& path)
	{
		auto mime = std::make_unique<QMimeData>();
		mime->setUrls({ QUrl::fromLocalFile(path) });
		return mime;
	}

	/**
	 * Reaches the drop handler directly.
	 *
	 * The drag-enter tests post their events and let Qt route them, which is what proves the handler
	 * is wired to the view's viewport at all. A Drop cannot be raised that way: Qt only delivers one
	 * to a widget that is mid-drag, and that state belongs to the platform's drag session, which a
	 * test has no way to start. So the drop *rules* are driven straight through the handler.
	 */
	class DroppableGraphView : public MaterialGraphView
	{
	public:
		using MaterialGraphView::dropEvent;
	};

	/** Whether the view would take this drag. Sent to the viewport, where Qt delivers one. */
	bool
	AcceptsDrag(MaterialGraphView& view, const QMimeData& mime)
	{
		QDragEnterEvent
			enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);

		// The reject path returns without calling ignore(), and a QDragEnterEvent arrives accepted, so
		// a test that does not clear it first cannot tell the two apart.
		enter.ignore();
		QCoreApplication::sendEvent(view.viewport(), &enter);

		return enter.isAccepted();
	}

	QDropEvent
	DropOf(const QMimeData& mime)
	{
		return QDropEvent(
			QPointF(10.0, 10.0),
			Qt::CopyAction,
			&mime,
			Qt::LeftButton,
			Qt::NoModifier);
	}
}

TEST_CASE("A material graph starts empty", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	REQUIRE(model.OutputNodeId() == InvalidNodeId);
	REQUIRE(model.OutputNode() == nullptr);
}

TEST_CASE("The sink is found by what it is", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode("Texture");
	const NodeId sink    = model.addNode("MaterialOutput");

	// Not by the order it was added in -- a loaded graph can have its nodes in any order.
	REQUIRE(model.OutputNodeId() == sink);
	REQUIRE(model.OutputNode() != nullptr);
	REQUIRE(texture != sink);
}

TEST_CASE("The sink cannot be deleted", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId sink = model.addNode("MaterialOutput");

	// A graph with nothing to compile into is not a material. Delete has to refuse rather than leave
	// the editor holding a graph it cannot save.
	REQUIRE(!model.deleteNode(sink));
	REQUIRE(model.OutputNodeId() == sink);
}

TEST_CASE("Any other node can be deleted", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode("Texture");

	REQUIRE(model.deleteNode(texture));
	REQUIRE(!model.allNodeIds().contains(texture));
}

TEST_CASE("Switching the output type replaces the sink", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId opaque = model.addNode("MaterialOutput");

	REQUIRE(model.SetOutputType("AlphaTestedMaterialOutput"));

	const NodeId cutout = model.OutputNodeId();
	REQUIRE(cutout != InvalidNodeId);
	REQUIRE(cutout != opaque);
	REQUIRE(model.OutputNode()->IsAlphaTested());

	// Exactly one sink, always: the old one is gone, not merely hidden behind the new one.
	REQUIRE(model.allNodeIds().size() == 1);
}

TEST_CASE("Switching to what the sink already is changes nothing", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId sink = model.addNode("MaterialOutput");

	// Rebuilding the sink would throw away its factors and its wires for nothing.
	REQUIRE(!model.SetOutputType("MaterialOutput"));
	REQUIRE(model.OutputNodeId() == sink);
}

TEST_CASE("Switching the output type of a graph with no sink fails", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	REQUIRE(!model.SetOutputType("AlphaTestedMaterialOutput"));
}

TEST_CASE("Switching the output type keeps where the sink was", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId sink = model.addNode("MaterialOutput");
	model.setNodeData(sink, NodeRole::Position, QPointF(120.0, -45.0));

	REQUIRE(model.SetOutputType("AlphaTestedMaterialOutput"));

	// Switching the output type is not a reason for the node to jump across the canvas.
	const QPointF position =
		model.nodeData(model.OutputNodeId(), NodeRole::Position).value<QPointF>();
	REQUIRE(position == QPointF(120.0, -45.0));
}

TEST_CASE("Switching the output type keeps the sink's settings", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	model.addNode("MaterialOutput");

	// Seeded on the delegate, because setNodeData ignores InternalData -- which is the same reason
	// SetOutputType cannot use it to carry the state across.
	QJsonObject state;
	state["metallic"]  = 0.25;
	state["roughness"] = 0.75;
	state["split"]     = QJsonArray{ true, false, false };
	model.OutputNode()->load(state);

	REQUIRE(model.SetOutputType("AlphaTestedMaterialOutput"));

	// The factors the artist dialled in belong to the material, not to the sink that happened to be
	// carrying them.
	const MaterialOutputNode* sink = model.OutputNode();
	REQUIRE(sink->MetallicFactor() == 0.25f);
	REQUIRE(sink->RoughnessFactor() == 0.75f);

	// And so does the port layout: a cutout's base colour splits into four, not the opaque three.
	REQUIRE(sink->nPorts(QtNodes::PortType::In) == 6u);
}

TEST_CASE("Switching the output type keeps a wire whose type still fits", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode("Texture");
	model.addNode("MaterialOutput");

	// The texture's RG port into the collapsed 2-wide normal port. Normal is 2 channels on both
	// sinks, so this is still a legal wire after the switch.
	const NodeId opaque = model.OutputNodeId();
	model.addConnection({ texture, 2, opaque, 2 });
	REQUIRE(model.allConnectionIds(opaque).size() == 1);

	REQUIRE(model.SetOutputType("AlphaTestedMaterialOutput"));

	const NodeId cutout = model.OutputNodeId();
	REQUIRE(model.allConnectionIds(cutout).size() == 1);

	const ConnectionId survived = *model.allConnectionIds(cutout).begin();
	REQUIRE(survived.outNodeId == texture);
	REQUIRE(survived.inPortIndex == QtNodes::PortIndex(2));
}

TEST_CASE("Switching the output type drops a wire whose type no longer fits", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	const NodeId texture = model.addNode("Texture");
	model.addNode("MaterialOutput");

	// The texture's RGB port into the opaque sink's 3-wide base colour. A cutout's base colour is
	// RGBA, so after the switch there is nothing this wire could legally mean -- carrying it over
	// would silently reinterpret an RGB bundle as RGBA.
	const NodeId opaque = model.OutputNodeId();
	model.addConnection({ texture, 1, opaque, 0 });
	REQUIRE(model.allConnectionIds(opaque).size() == 1);

	REQUIRE(model.SetOutputType("AlphaTestedMaterialOutput"));

	REQUIRE(model.allConnectionIds(model.OutputNodeId()).empty());
}

TEST_CASE("The scene menu does not offer a sink", "[materialgraph]")
{
	MaterialGraphModel model(Registry());
	MaterialGraphScene scene(model);

	const std::unique_ptr<QMenu> menu(scene.createSceneMenu(QPointF(0.0, 0.0)));

	auto* tree = menu->findChild<QTreeWidget*>();
	REQUIRE(tree != nullptr);

	// A sink is switched, not added. It stays registered -- the graph creates one by name, and loads
	// one from a saved graph -- but it must not show up as something to drop on the canvas, or a
	// graph could end up with two.
	REQUIRE(tree->findItems(QLatin1String(c_OutputCategory), Qt::MatchExactly).isEmpty());

	// Dropping the sinks must not take the rest of the menu with it.
	REQUIRE(!tree->findItems("Input", Qt::MatchExactly).isEmpty());
}

TEST_CASE("A texture dragged onto the graph is accepted", "[materialgraph]")
{
	MaterialGraphView view;

	REQUIRE(AcceptsDrag(view, *UrlDrop("C:/Data/Textures/albedo.ktx2")));
}

TEST_CASE("Anything else dragged onto the graph is ignored", "[materialgraph]")
{
	MaterialGraphView view;

	// A mesh is not a texture. The graph has no port to hang it on.
	REQUIRE(!AcceptsDrag(view, *UrlDrop("C:/Data/Meshes/tree.bmesh")));
}

TEST_CASE("A dropped texture is announced with its path", "[materialgraph]")
{
	DroppableGraphView view;
	QSignalSpy         dropped(&view, &MaterialGraphView::TextureDropped);

	// Upper case on purpose: the suffix test is case-insensitive, because what a file is named has
	// nothing to do with what it is.
	const std::unique_ptr<QMimeData> mime = UrlDrop("C:/Data/Textures/Albedo.KTX2");

	QDropEvent drop = DropOf(*mime);
	view.dropEvent(&drop);

	REQUIRE(dropped.count() == 1);
	REQUIRE(dropped.front().at(0).toString() == QString("C:/Data/Textures/Albedo.KTX2"));
	REQUIRE(drop.isAccepted());
}

TEST_CASE("The first texture in a drop wins", "[materialgraph]")
{
	DroppableGraphView view;
	QSignalSpy         dropped(&view, &MaterialGraphView::TextureDropped);

	QMimeData mime;
	mime.setUrls(
		{ QUrl::fromLocalFile("C:/Data/Meshes/tree.bmesh"),
	      QUrl::fromLocalFile("C:/Data/Textures/bark.ktx2"),
	      QUrl::fromLocalFile("C:/Data/Textures/leaf.ktx2") });

	QDropEvent drop = DropOf(mime);
	view.dropEvent(&drop);

	// One drop makes one node, and the non-texture in the list is passed over rather than
	// disqualifying the whole drop.
	REQUIRE(dropped.count() == 1);
	REQUIRE(dropped.front().at(0).toString() == QString("C:/Data/Textures/bark.ktx2"));
}

//
// Centring the graph on its sink
//

TEST_CASE("A graph with no sink has nowhere to centre", "[materialgraph]")
{
	MaterialGraphModel model(Registry());
	model.addNode("Texture");

	REQUIRE_FALSE(MaterialEditorWindow::OutputCentre(model).has_value());
}

TEST_CASE("The graph centres on the middle of the sink, not its corner", "[materialgraph]")
{
	MaterialGraphModel model(Registry());

	// A node is measured by its graphics object, so it has no size until it is in a scene -- which is
	// the state the editor centres from, because ResetGraph builds the scene before it centres.
	MaterialGraphScene scene(model);

	const NodeId  sink = model.addNode("MaterialOutput");
	const QPointF pos(220.0, 40.0);
	model.setNodeData(sink, NodeRole::Position, pos);

	const auto centre = MaterialEditorWindow::OutputCentre(model);
	REQUIRE(centre.has_value());

	// Centred by its corner, the node hangs off the left of the panel.
	const QSize size = model.nodeData(sink, NodeRole::Size).value<QSize>();
	REQUIRE(size.isValid());
	REQUIRE(size.width() > 0);

	CHECK(centre->x() == Catch::Approx(pos.x() + size.width() / 2.0));
	CHECK(centre->y() == Catch::Approx(pos.y() + size.height() / 2.0));
}

TEST_CASE("An unmeasured sink centres on its corner", "[materialgraph]")
{
	// No scene, so no graphics object, so QtNodes reports the node's size as -1 x -1. Half of that
	// would centre just off the corner -- worse than the corner itself.
	MaterialGraphModel model(Registry());

	const NodeId  sink = model.addNode("MaterialOutput");
	const QPointF pos(220.0, 40.0);
	model.setNodeData(sink, NodeRole::Position, pos);

	REQUIRE_FALSE(model.nodeData(sink, NodeRole::Size).value<QSize>().isValid());

	const auto centre = MaterialEditorWindow::OutputCentre(model);
	REQUIRE(centre.has_value());
	CHECK(*centre == pos);
}

TEST_CASE("Resizing the view keeps the sink centred", "[materialgraph]")
{
	MaterialGraphModel model(Registry());
	MaterialGraphScene scene(model);
	MaterialGraphView  view;

	const NodeId sink = model.addNode("MaterialOutput");
	model.setNodeData(sink, NodeRole::Position, QPointF(2000.0, 1500.0));

	view.setScene(&scene);

	// Room to scroll: with a scene that fits inside the viewport there is nothing to centre, and the
	// test would pass without the view ever having done anything.
	view.setSceneRect(QRectF(0.0, 0.0, 4000.0, 3000.0));
	view.resize(400, 300);

	const auto centre = MaterialEditorWindow::OutputCentre(model);
	REQUIRE(centre.has_value());

	view.centerOn(*centre);
	REQUIRE((view.mapFromScene(*centre) - view.viewport()->rect().center()).manhattanLength() <= 2);

	// The graph is built while the panel is still being laid out, so the view is centred at the wrong
	// size and then resized. QGraphicsView pins the top-left through a resize by default, which would
	// slide the sink off-centre by half the size change; MaterialGraphView anchors the centre instead.
	view.resize(900, 700);

	const QPoint offset = view.mapFromScene(*centre) - view.viewport()->rect().center();
	INFO("sink is " << offset.manhattanLength() << "px off centre after the resize");
	CHECK(offset.manhattanLength() <= 2);
}
