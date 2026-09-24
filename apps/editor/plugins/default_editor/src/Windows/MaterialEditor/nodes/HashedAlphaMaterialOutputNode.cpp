#include "Windows/MaterialEditor/nodes/HashedAlphaMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include <editor_plugin_api/ILanguageResolver.h>

#include <QFormLayout>
#include <QLabel>
#include <editor_plugin_api/localize.h>
#include <qwidget.h>

HashedAlphaMaterialOutputNode::HashedAlphaMaterialOutputNode(
	const editor::ILanguageResolver& language) :
	MaterialOutputNode(
		language,
		ChannelData::c_MaxChannels)  // base color is RGBA: the alpha is the coverage
{}

void
HashedAlphaMaterialOutputNode::AddExtraRows(QWidget* parent, QFormLayout* form)
{
	// No cutoff row, deliberately -- there is no threshold to author. Said here because its absence
	// beside the other two sinks reads as an omission otherwise.
	auto* note = new QLabel(
		editor::Localize(
			m_Language,
			"bernini.material_nodes.hashed_alpha_note",
			"Coverage is stochastic; needs\ntemporal antialiasing to resolve."),
		parent);
	note->setEnabled(false);
	form->addRow(
		editor::Localize(m_Language, "bernini.material_nodes.hashed_alpha_row_label", "Alpha"),
		note);
}
