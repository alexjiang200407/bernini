#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/pak_pack.h>

class Project
{
public:
	static constexpr auto c_FileExtension = ".berniniproject";

	static constexpr auto c_MeshesDirectoryName      = "Meshes";
	static constexpr auto c_TexturesDirectoryName    = "Textures";
	static constexpr auto c_TexturesSrcDirectoryName = "textures_src";
	static constexpr auto c_MaterialsDirectoryName   = "Materials";
	static constexpr auto c_LevelsDirectoryName      = "Levels";

	// One per environment container, because the three have different lifetimes: a sky is re-authored
	// in seconds, the lighting convolved from it takes minutes, and the `.benv` naming the pair is a
	// few bytes that outlives both.
	static constexpr auto c_EnvironmentsDirectoryName = "Environments";
	static constexpr auto c_EnvLightingDirectoryName  = "EnvLighting";
	static constexpr auto c_SkyDirectoryName          = "Sky";

	// Split for the same reason, and a sharper one: a rig outlives its clips. Re-cooking a clip set
	// leaves the skeleton untouched, and re-authoring a rest pose does not invalidate a clip -- which
	// is what skeletonSignature exists to check, and only means anything if the two can move apart.
	static constexpr auto c_SkeletonsDirectoryName  = "Skeletons";
	static constexpr auto c_AnimationsDirectoryName = "Animations";

	/**
	 * Every category Create scaffolds and IsRequiredDirectory protects, in one place -- anything that
	 * needs to know the layout reads it here rather than restating it and drifting.
	 */
	static constexpr std::array<std::string_view, 10> c_RequiredDirectories = {
		c_MeshesDirectoryName,      c_TexturesDirectoryName, c_TexturesSrcDirectoryName,
		c_MaterialsDirectoryName,   c_LevelsDirectoryName,   c_EnvironmentsDirectoryName,
		c_EnvLightingDirectoryName, c_SkyDirectoryName,      c_SkeletonsDirectoryName,
		c_AnimationsDirectoryName,
	};

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
	 * Only the categories themselves. A folder the user made inside one, like `Materials/kirk`, is
	 * theirs.
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

	/** Where the project's archive is, whether or not one has been packed. */
	[[nodiscard]] std::filesystem::path
	GetArchiveFile() const noexcept
	{
		return m_ProjectFile.parent_path() / assetlib::c_DefaultArchiveName;
	}

	/**
	 * What the project reads its assets through, and writes them to.
	 *
	 * The loose `Data/` tree over `Data.bpak` when one has been packed, loose alone when none has.
	 * Both are the same to a reader; the difference is only that a packed project can ship. Writes
	 * always land on `Data/` -- an archive entry cannot be replaced in place, which is what makes
	 * the loose layer an overlay rather than a cache.
	 *
	 * Rebuilt by ReloadStore, which is what a pack or a *Sync* during the session calls.
	 */
	[[nodiscard]] const assetlib::AssetStore&
	GetStore() const noexcept
	{
		// Create and Open both mount before they hand a Project back, so this holds for every
		// Project that exists. It is an invariant of those two functions rather than of the type,
		// which is what the assert is for.
		assert(
			m_Store.has_value() &&
			"a Project is mounted by Create or Open before it is handed out");
		return *m_Store;
	}

	/** Re-reads the archive, picking up one that has just been packed or replaced. */
	void
	ReloadStore();

private:
	Project() = default;

	static constexpr auto c_DataDirectoryName = "Data";
	static constexpr auto c_FormatVersion     = 1;

	// Held rather than built per call: mounting an archive reads its header and entry table, which
	// is not something an accessor should do. optional because a Project is default-constructed
	// before Open fills it in.
	std::optional<assetlib::AssetStore> m_Store;

	std::string           m_Name;
	std::filesystem::path m_ProjectFile;
	int                   m_FormatVersion = c_FormatVersion;
};
