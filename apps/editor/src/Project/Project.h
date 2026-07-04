#pragma once

class Project
{
public:
	static constexpr auto c_FileExtension = ".berniniproject";

	/**
	 * Creates a new project on disk: scaffolds the Data directory tree
	 * (Meshes, Textures, Materials) and writes the project metadata file.
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

	const std::string&
	GetName() const noexcept
	{
		return m_name;
	}

	const std::filesystem::path&
	GetProjectFile() const noexcept
	{
		return m_projectFile;
	}

	std::filesystem::path
	GetDataDirectory() const noexcept
	{
		return m_projectFile.parent_path() / c_DataDirectoryName;
	}

private:
	Project() = default;

	static constexpr auto c_DataDirectoryName = "Data";
	static constexpr auto c_FormatVersion     = 1;

	std::string           m_name;
	std::filesystem::path m_projectFile;
	int                   m_formatVersion = c_FormatVersion;
};
