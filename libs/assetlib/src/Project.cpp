#include <algorithm>
#include <assetlib/AssetKindRegistry.h>
#include <assetlib/Project.h>
#include <assetlib/project_layout.h>
#include <core/file/LooseFileSystem.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace assetlib
{
	namespace
	{
		struct ProjectMetadata
		{
			std::string              name;
			std::vector<std::string> plugins;
			int                      version = 1;
		};

		ProjectMetadata
		readMetadata(const std::filesystem::path& projectFile, const int currentVersion)
		{
			std::ifstream stream(projectFile);
			if (!stream)
				throw std::runtime_error("Cannot open project file: " + projectFile.string());

			nlohmann::json json;
			try
			{
				stream >> json;
				ProjectMetadata metadata;
				metadata.name    = json.value("name", projectFile.stem().string());
				metadata.version = json.value("version", currentVersion);
				metadata.plugins = json.value("plugins", std::vector<std::string>());
				return metadata;
			}
			catch (const nlohmann::json::exception& e)
			{
				throw std::runtime_error("Malformed project file: " + std::string(e.what()));
			}
		}
	}

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
	Project::Open(
		const std::filesystem::path&       projectFile,
		std::shared_ptr<AssetKindRegistry> registry)
	{
		const ProjectMetadata metadata = readMetadata(projectFile, c_FormatVersion);

		Project project;
		project.m_ProjectFile   = projectFile;
		project.m_Name          = metadata.name;
		project.m_PluginIds     = metadata.plugins;
		project.m_FormatVersion = metadata.version;
		if (registry != nullptr)
			project.m_Registry = std::move(registry);

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

	std::vector<std::string>
	Project::PluginIdsOf(const std::filesystem::path& projectFile)
	{
		return readMetadata(projectFile, c_FormatVersion).plugins;
	}

	void
	Project::Save() const
	{
		const nlohmann::json json = {
			{ "name", m_Name },
			{ "version", m_FormatVersion },
			{ "dataDirectory", c_DataDirectoryName },
			{ "plugins", m_PluginIds },
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
		m_Store.emplace(
			GetDataDirectory(),
			std::make_shared<const core::file::LooseFileSystem>(GetDataDirectory()),
			m_Registry);
	}
}
