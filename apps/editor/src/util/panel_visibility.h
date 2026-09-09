#pragma once

class QWidget;

namespace editor
{
	/**
	 * Whether the dock that just reported `dockVisible` is still the panel the editor is showing.
	 *
	 * `QDockWidget::visibilityChanged(false)` says two different things: the tab was deselected or
	 * the dock closed, and the window it lives in minimized or hid. A minimized editor is still on
	 * whatever tab it was on, so this holds the panel shown until the window comes back -- a panel
	 * that tore its mesh down for a minimize would lose work nobody asked it to lose.
	 *
	 * A null `window` reads as shown: nothing is known, so nothing is thrown away.
	 */
	[[nodiscard]] bool
	IsPanelShown(bool dockVisible, const QWidget* window) noexcept;
}
