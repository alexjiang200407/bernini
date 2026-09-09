#include "util/panel_visibility.h"

#include <QWidget>

namespace editor
{
	bool
	IsPanelShown(const bool dockVisible, const QWidget* window) noexcept
	{
		if (dockVisible || window == nullptr)
			return true;

		// isVisible stays true for a minimized window, so neither test stands in for the other.
		return !window->isVisible() || window->isMinimized();
	}
}
