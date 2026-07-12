#pragma once

#include <QtNodes/DataFlowGraphModel>

class MaterialOutputNode;

// The registry category the sink nodes are registered under. MaterialGraphScene hides it from the
// context menu, so the sink is chosen from the toolbar rather than added like an ordinary node.
inline constexpr char c_OutputCategory[] = "Output";

/**
 * A material graph: an ordinary QtNodes data-flow graph with exactly one sink, always.
 *
 * The sink is what the material compiles from, and the compiler takes the first one it finds -- so a
 * graph with two sinks compiles from an arbitrary one, and a graph with none compiles from nothing.
 * Neither fails loudly; both just produce the wrong material. This model makes both unreachable: the
 * sink cannot be deleted, and SetOutputType swaps it in place instead of adding another.
 */
class MaterialGraphModel : public QtNodes::DataFlowGraphModel
{
	Q_OBJECT

public:
	using QtNodes::DataFlowGraphModel::DataFlowGraphModel;

	// The graph's sink, or nullptr if it somehow has none
	[[nodiscard]] MaterialOutputNode*
	OutputNode();

	[[nodiscard]] QtNodes::NodeId
	OutputNodeId();

	/**
	 * Replaces the sink with the registered model named `modelName` -- this is how a material is
	 * switched between opaque and alpha-tested.
	 *
	 * @return whether the sink changed (false if it was already this type, or there is no sink).
	 */
	bool
	SetOutputType(const QString& modelName);

	// Refuses to delete the sink; see the class comment.
	bool
	deleteNode(QtNodes::NodeId nodeId) override;

private:
	// Whether `connection` could be made by hand: the port exists, and the two ends agree on a type.
	[[nodiscard]] bool
	PortsAreCompatible(const QtNodes::ConnectionId& connection) const;

	// Lets SetOutputType through the deleteNode guard, and nothing else.
	bool m_ReplacingOutput = false;
};
