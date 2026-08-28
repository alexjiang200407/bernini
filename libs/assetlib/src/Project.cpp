#include <assetlib/Project.h>

#include <core/file/LooseFileSystem.h>
#include <nlohmann/json.hpp>

namespace assetlib
{
	bool
	Project::IsRequiredDirectory(const std::filesystem::path& relativeToData)
	{
		std::string path = relativeToData.lexically_normal().generic_string();

		// `a/b/..` normalizes to `a/`, and every name below is spelled without the slash.
		if (path.size() > 1 && path.back() == '/')
			path.pop_back();

		// The data root itself, however it was spelled to get here.
		if (path.empty() || path == "." || path == "/")
			return true;

		// Either half: deleting one would take every category under it.
		if (path == c_AuthoredDirectoryName || path == c_DerivedDirectoryName)
			return true;

		return std::ranges::find(c_RequiredDirectories, path) != c_RequiredDirectories.end();
	}

	Project
	Project::Create(const std::filesystem::path& projectFile, std::string_view name)
	{
		const auto root = projectFile.parent_path();

		std::error_code ec;
		std::filesystem::create_directories(root, ec);
		if (ec)
			throw std::runtime_error("Failed to create project directory: " + root.string());

		for (const auto category : c_RequiredDirectories)
		{
			std::filesystem::create_directories(root / c_DataDirectoryName / category, ec);
			if (ec)
				throw std::runtime_error(
					"Failed to create data directory: " + std::string(category));
		}

		Project project;
		project.m_Name          = std::string(name);
		project.m_ProjectFile   = projectFile;
		project.m_FormatVersion = c_FormatVersion;
		project.Save();
		project.ReloadStore();

		return project;
	}

	Project
	Project::Open(const std::filesystem::path& projectFile)
	{
		std::ifstream stream(projectFile);
		if (!stream)
			throw std::runtime_error("Cannot open project file: " + projectFile.string());

		nlohmann::json json;
		try
		{
			stream >> json;
		}
		catch (const nlohmann::json::exception& e)
		{
			throw std::runtime_error("Malformed project file: " + std::string(e.what()));
		}

		Project project;
		project.m_ProjectFile   = projectFile;
		project.m_Name          = json.value("name", projectFile.stem().string());
		project.m_FormatVersion = json.value("version", static_cast<int>(c_FormatVersion));

		const auto root = projectFile.parent_path();
		for (const auto category : c_RequiredDirectories)
		{
			auto dir = root / c_DataDirectoryName / category;

			if (std::filesystem::exists(dir))
			{
				if (!std::filesystem::is_directory(dir))
					throw std::runtime_error(
						std::format(
							"Data directory is not a directory: {}, please consider deleting "
							"manually",
							dir.string()));

				continue;
			}

			std::error_code ec;
			std::filesystem::create_directories(root / c_DataDirectoryName / category, ec);

			if (ec)
				throw std::runtime_error(
					"Failed to create data directory: " + std::string(category));
		}

		project.ReloadStore();

		return project;
	}

	void
	Project::Save() const
	{
		const nlohmann::json json = {
			{ "name", m_Name },
			{ "version", m_FormatVersion },
			{ "dataDirectory", c_DataDirectoryName },
		};

		std::ofstream stream(m_ProjectFile);
		if (!stream)
			throw std::runtime_error("Cannot write project file: " + m_ProjectFile.string());

		stream << json.dump(4);
	}

	void
	Project::ReloadStore()
	{
		// Loose, always. The editor authors the tree, not the archive: assets are version-tracked as
		// separate files, and one packed blob is the wrong unit for that. An archive is what `pack`
		// makes from this tree to ship, and what a shipped game mounts -- it is never read back here,
		// so an asset the editor lists is always an asset the editor can write.
		m_Store.emplace(GetDataDirectory());
	}
}
