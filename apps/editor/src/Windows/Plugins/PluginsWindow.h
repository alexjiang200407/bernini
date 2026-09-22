#pragma once

#include "Plugins/plugin_loader.h"
#include <QWidget>
#include <qtmetamacros.h>

namespace editor
{
	/**
	 * The loaded plugins, one row each: name and description, with the ID, directory and modules in
	 * the row's tooltip. Filled once at construction, since the set cannot change without a relaunch.
	 */
	class PluginsWindow : public QWidget
	{
		Q_OBJECT

	public:
		explicit PluginsWindow(const plugins::PluginSession& session, QWidget* parent = nullptr);
	};
}
