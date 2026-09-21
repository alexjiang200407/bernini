#pragma once

#include <QWidget>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	/** GUI-thread only. The host owns the widget and destroys it before its project services. */
	class EditorPanel : public QWidget
	{
	public:
		using QWidget::QWidget;

		/** Includes displayed assets as well as edited ones; all paths are mount keys. */
		[[nodiscard]] virtual std::vector<std::string>
		GetHeldAssets() const = 0;

		/** May prompt to save; a refusal cancels closing the project, not just this tab. */
		[[nodiscard]] virtual bool
		CanClose() = 0;

		/** Tab selection, not QWidget visibility; inactive tabs must suspend their viewports. */
		virtual void
		SetActive(bool active) = 0;

		/** Queued on the GUI thread; key is borrowed for this call. Default: no cached state. */
		virtual void
		OnAssetChanged(std::string_view key)
		{
			static_cast<void>(key);
		}
	};

	class AssetEditorPanel : public EditorPanel
	{
	public:
		using EditorPanel::EditorPanel;

		/** A normalized mount key. A refused replacement must leave the previous document intact. */
		virtual void
		OpenAsset(std::string_view key) = 0;
	};
}
