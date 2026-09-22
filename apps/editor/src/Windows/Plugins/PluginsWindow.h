#pragma once

#include "Plugins/plugin_loader.h"
#include <QWidget>
#include <qtmetamacros.h>

class QTreeWidget;

namespace editor
{
	/**
	 * What the editor loaded: every plugin, where it came from, which modules it loaded and what it
	 * contributed, then the configured directories the project did not ask for. Filled once at
	 * construction, since the set cannot change without a relaunch.
	 */
	class PluginsWindow : public QWidget
	{
		Q_OBJECT

	public:
		PluginsWindow(
			const plugins::PluginSession& session,
			const plugins::BuildIdentity& build,
			QWidget*                      parent = nullptr);

	private:
		QTreeWidget* m_Tree = nullptr;
	};
}
