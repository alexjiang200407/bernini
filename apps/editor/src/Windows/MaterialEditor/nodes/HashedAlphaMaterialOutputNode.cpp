#include "Windows/MaterialEditor/nodes/HashedAlphaMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include <QFormLayout>
#include <QLabel>
#include <qstringliteral.h>
#include <qwidget.h>

HashedAlphaMaterialOutputNode::HashedAlphaMaterialOutputNode() :
	MaterialOutputNode(ChannelData::c_MaxChannels)  // base color is RGBA: the alpha is the coverage
{}

void
HashedAlphaMaterialOutputNode::AddExtraRows(QWidget* parent, QFormLayout* form)
{
	// No cutoff row, deliberately -- there is no threshold to author. Said here because its absence
	// beside the other two sinks reads as an omission otherwise.
	auto* note = new QLabel(
		QStringLiteral("Coverage is stochastic; needs\ntemporal antialiasing to resolve."),
		parent);
	note->setEnabled(false);
	form->addRow(QStringLiteral("Alpha"), note);
}
