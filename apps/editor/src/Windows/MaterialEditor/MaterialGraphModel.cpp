#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include <QJsonObject>
#include <QPointF>
#include <qobject.h>
#include <unordered_set>
#include <vector>

#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include <QtNodes/internal/DataFlowGraphModel.hpp>
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>

using QtNodes::ConnectionId;
using QtNodes::InvalidNodeId;
using QtNodes::InvalidPortIndex;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortRole;
using QtNodes::PortType;

QtNodes::NodeId
MaterialGraphModel::OutputNodeId()
{
	for (const NodeId nodeId : allNodeIds())
	{
		if (delegateModel<MaterialSinkNode>(nodeId) != nullptr)
			return nodeId;
	}
	return InvalidNodeId;
}

MaterialSinkNode*
MaterialGraphModel::OutputNode()
{
	const NodeId nodeId = OutputNodeId();
	return nodeId == InvalidNodeId ? nullptr : delegateModel<MaterialSinkNode>(nodeId);
}

bool
MaterialGraphModel::deleteNode(NodeId nodeId)
{
	if (!m_ReplacingOutput && delegateModel<MaterialSinkNode>(nodeId) != nullptr)
		return false;

	return DataFlowGraphModel::deleteNode(nodeId);
}

bool
MaterialGraphModel::PortsAreCompatible(const ConnectionId& connection) const
{
	const auto inPortCount = nodeData(connection.inNodeId, NodeRole::InPortCount).toUInt();
	if (static_cast<unsigned int>(connection.inPortIndex) >= inPortCount)
		return false;

	const auto out =
		portData(connection.outNodeId, PortType::Out, connection.outPortIndex, PortRole::DataType)
			.value<QtNodes::NodeDataType>();
	const auto in =
		portData(connection.inNodeId, PortType::In, connection.inPortIndex, PortRole::DataType)
			.value<QtNodes::NodeDataType>();

	return out.id == in.id && SinkAccepts(connection);
}

bool
MaterialGraphModel::SinkAccepts(const QtNodes::ConnectionId& connection) const
{
	// A data slot is bound whole or composited from routes, never both (ADR-7): the sink refuses
	// the second kind while the first is wired.
	// delegateModel has no const overload in the vendored QtNodes; this reads only.
	if (const auto* sink = const_cast<MaterialGraphModel*>(this)->delegateModel<SurfaceOutputNode>(
			connection.inNodeId))
		return sink->PortAccepts(connection.inPortIndex);

	return true;
}

bool
MaterialGraphModel::connectionPossible(QtNodes::ConnectionId const connectionId) const
{
	return DataFlowGraphModel::connectionPossible(connectionId) && SinkAccepts(connectionId);
}

bool
MaterialGraphModel::SetOutputType(const QString& modelName)
{
	const NodeId oldId = OutputNodeId();
	if (oldId == InvalidNodeId)
		return false;

	const MaterialSinkNode* old = delegateModel<MaterialSinkNode>(oldId);
	if (old->name() == modelName)
		return false;

	const QPointF position = nodeData(oldId, NodeRole::Position).value<QPointF>();

	// Straight off the delegate, not through nodeData(InternalData): that wraps the state in an
	// "internal-data" envelope, and load() expects the state itself.
	const QJsonObject state = old->save();

	// Its index follows the PBR sink's group ports, which differ between sinks, so it is moved by
	// what it is rather than by where it was. A surface's sink has none.
	const auto*     oldPbr     = qobject_cast<const MaterialOutputNode*>(old);
	const PortIndex oldUv1Port = oldPbr != nullptr ? oldPbr->Uv1OcclusionPort() : InvalidPortIndex;

	const std::unordered_set<ConnectionId> wires = allConnectionIds(oldId);
	const std::vector<ConnectionId>        incoming(wires.begin(), wires.end());

	m_ReplacingOutput = true;
	const bool erased = deleteNode(oldId);
	m_ReplacingOutput = false;
	if (!erased)
		return false;

	const NodeId newId = addNode(modelName);
	if (newId == InvalidNodeId)
		return false;

	setNodeData(newId, NodeRole::Position, position);

	// Hand the state to the delegate itself. DataFlowGraphModel::setNodeData ignores InternalData
	// outright, so going through it would silently drop the factors and the split layout the artist
	// had dialled in -- and switching a material between opaque and cutout would quietly reset it.
	// Loading straight after the node is created is what QtNodes' own loadNode does.
	MaterialSinkNode* sink = delegateModel<MaterialSinkNode>(newId);
	if (sink != nullptr)
		sink->load(state);

	const auto*     newPbr     = qobject_cast<const MaterialOutputNode*>(sink);
	const PortIndex newUv1Port = newPbr != nullptr ? newPbr->Uv1OcclusionPort() : InvalidPortIndex;

	for (const ConnectionId& wire : incoming)
	{
		const bool      isUv1  = oldUv1Port != InvalidPortIndex && wire.inPortIndex == oldUv1Port;
		const PortIndex inPort = isUv1 ? newUv1Port : wire.inPortIndex;
		if (inPort == InvalidPortIndex || (!isUv1 && inPort == newUv1Port))
			continue;

		const ConnectionId moved{ wire.outNodeId, wire.outPortIndex, newId, inPort };
		if (PortsAreCompatible(moved))
			addConnection(moved);
	}

	return true;
}
