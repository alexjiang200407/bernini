#pragma once

#include <QDialog>
#include <QString>

#include <assetlib/Project.h>

#include <filesystem>
#include <optional>
#include <vector>

class QListWidget;
class QWidget;

namespace editor
{
	/**
	 * The landing page: what the editor shows when it starts without a project. It lists recent
	 * projects and offers New and Open, and it builds no renderer, since which shaders the renderer
	 * compiles depends on the project chosen here.
	 *
	 * Accepted once a project has opened. Rejected when the user closes it, which quits the editor.
	 */
	class ProjectLauncher : public QDialog
	{
	public:
		/**
		 * @param recent  The projects to offer, most recent first.
		 * @param notice  Shown above them: why the project the editor was told to open did not.
		 */
		explicit ProjectLauncher(
			std::vector<std::filesystem::path> recent,
			const QString&                     notice = {},
			QWidget*                           parent = nullptr);

		/** The project that opened. Valid once, and only after the launcher was accepted. */
		[[nodiscard]] assetlib::Project
		TakeProject();

	private:
		void
		NewProject();

		void
		OpenProject();

		// Opens `projectFile` and accepts, or reports why it could not and stays up.
		void
		OpenAt(const std::filesystem::path& projectFile);

		std::vector<std::filesystem::path> m_Recent;
		QListWidget*                       m_RecentList = nullptr;
		std::optional<assetlib::Project>   m_Project;
	};
}
