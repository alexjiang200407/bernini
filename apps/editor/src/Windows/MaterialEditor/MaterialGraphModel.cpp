#include "Windows/MaterialEditor/MaterialGraphModel.h"

#include <QJsonObject>
#include <QPointF>

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

using QtNodes::ConnectionId;
using QtNodes::InvalidNodeId;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortRole;
using QtNodes::PortType;

QtNodes::NodeId
MaterialGraphModel::OutputNodeId()
{
	for (const NodeId nodeId : allNodeIds())
	{
		if (delegateModel<MaterialOutputNode>(nodeId) != nullptr)
			return nodeId;
	}
	return InvalidNodeId;
}

MaterialOutputNode*
MaterialGraphModel::OutputNode()
{
	const NodeId nodeId = OutputNodeId();
	return nodeId == InvalidNodeId ? nullptr : delegateModel<MaterialOutputNode>(nodeId);
}

bool
MaterialGraphModel::deleteNode(NodeId nodeId)
{
	if (!m_ReplacingOutput && delegateModel<MaterialOutputNode>(nodeId) != nullptr)
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

	return out.id == in.id;
}

bool
MaterialGraphModel::SetOutputType(const QString& modelName)
{
	const NodeId oldId = OutputNodeId();
	if (oldId == InvalidNodeId)
		return false;

	const MaterialOutputNode* old = delegateModel<MaterialOutputNode>(oldId);
	if (old->name() == modelName)
		return false;

	const QPointF     position = nodeData(oldId, NodeRole::Position).value<QPointF>();
	const QJsonObject state    = nodeData(oldId, NodeRole::InternalData).value<QJsonObject>();

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

	setNodeData(newId, NodeRole::InternalData, state);

	for (const ConnectionId& wire : incoming)
	{
		const ConnectionId moved{ wire.outNodeId, wire.outPortIndex, newId, wire.inPortIndex };
		if (PortsAreCompatible(moved))
			addConnection(moved);
	}

	return true;
}
