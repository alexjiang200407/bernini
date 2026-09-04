#pragma once
#include <assetlib/AssetStore.h>
#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace assetlib
{
	class Project
	{
	public:
		static constexpr auto c_FileExtension = ".bproj";

		/**
		 * Creates a new project on disk: scaffolds the Data directory tree
		 * (one directory per asset category) and writes the project metadata file.
		 *
		 * The parent directory of projectFile becomes the project root and is
		 * created if it does not already exist.
		 *
		 * @param projectFile Destination path of the project file (its parent is the project root).
		 * @param name Human-readable project name stored in the metadata.
		 * @return The created project.
		 * @throws std::runtime_error if any directory or the project file cannot be written.
		 */
		static Project
		Create(const std::filesystem::path& projectFile, std::string_view name);

		/**
		 * Opens an existing project by reading and parsing its project file.
		 *
		 * @param projectFile Path to an existing project file.
		 * @return The loaded project.
		 * @throws std::runtime_error if the file is missing or malformed.
		 */
		static Project
		Open(const std::filesystem::path& projectFile);

		/**
		 * Writes the current metadata back to the project file.
		 *
		 * @throws std::runtime_error if the project file cannot be written.
		 */
		void
		Save() const;

		/**
		 * Whether `relativeToData` is one of the directories the project is scaffolded with -- the data root
		 * itself, or one of the categories beneath it -- and so is not the user's to delete. Open() puts a
		 * missing one straight back, so deleting one would not even stick.
		 *
		 * Only the categories themselves. A folder the user made inside one, like
		 * `Authored/Materials/kirk`, is theirs.
		 */
		[[nodiscard]] static bool
		IsRequiredDirectory(const std::filesystem::path& relativeToData);

		const std::string&
		GetName() const noexcept
		{
			return m_Name;
		}

		const std::filesystem::path&
		GetProjectFile() const noexcept
		{
			return m_ProjectFile;
		}

		std::filesystem::path
		GetDataDirectory() const noexcept
		{
			return m_ProjectFile.parent_path() / c_DataDirectoryName;
		}

		/**
		 * What the project reads its assets through, and writes them to: the loose `Data/` tree.
		 *
		 * Never an archive. The editor authors the tree -- separate files, which is the unit version
		 * control wants -- and `pack` turns that tree into an archive to ship. Nothing reads one back
		 * here, so every asset the editor lists is one the editor can write.
		 */
		[[nodiscard]] const AssetStore&
		GetStore() const noexcept
		{
			// Create and Open both fill the store in before they hand a Project back, so this holds for
			// every Project that exists. It is an invariant of those two functions rather than of the
			// type, which is what the assert is for.
			assert(
				m_Store.has_value() &&
				"a Project's store is built by Create or Open before it is handed out");
			return *m_Store;
		}

		/** Re-points the store at the data directory. */
		void
		ReloadStore();

	private:
		Project() = default;

		static constexpr auto c_DataDirectoryName = "Data";
		static constexpr auto c_FormatVersion     = 1;

		// Held rather than built per call: GetStore hands out a reference and is noexcept, and the
		// constructor throws on a data root that has gone. optional because a Project is
		// default-constructed before Open fills it in.
		std::optional<AssetStore> m_Store;

		std::string           m_Name;
		std::filesystem::path m_ProjectFile;
		int                   m_FormatVersion = c_FormatVersion;
	};
}
