#include "io/json_doc.h"
#include <algorithm>
#include <array>
#include <assetlib/AssetKindRegistry.h>
#include <assetlib/Project.h>
#include <assetlib/project_layout.h>
#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <nlohmann/json.hpp>
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
			int                      version     = 1;
			ToneMapping              toneMapping = ToneMapping::kAgX;
			nlohmann::json           unknown     = nlohmann::json::object();
		};

		constexpr std::string_view c_ToneMappingKey = "toneMapping";

		[[nodiscard]] std::string_view
		toneMappingName(const ToneMapping toneMapping) noexcept
		{
			return toneMapping == ToneMapping::kStandard ? "standard" : "agx";
		}

		ProjectMetadata
		readMetadata(const std::filesystem::path& projectFile, const int currentVersion)
		{
			std::ifstream stream(projectFile);
			if (!stream)
				core::throw_runtime_error("Cannot open project file: {}", projectFile.string());

			nlohmann::json json;
			try
			{
				stream >> json;
				ProjectMetadata metadata;
				metadata.name    = json.value("name", projectFile.stem().string());
				metadata.version = json.value("version", currentVersion);
				metadata.plugins = json.value("plugins", std::vector<std::string>());

				if (const auto it = json.find(c_ToneMappingKey); it != json.end())
				{
					const std::string value = it->is_string() ? it->get<std::string>() : "";
					if (value == "agx")
						metadata.toneMapping = ToneMapping::kAgX;
					else if (value == "standard")
						metadata.toneMapping = ToneMapping::kStandard;
					else
						core::throw_runtime_error(
							"Malformed project file: '{}' is {}, not \"agx\" or \"standard\"",
							c_ToneMappingKey,
							it->dump());
				}

				constexpr std::array<std::string_view, 5> c_KnownKeys = {
					{ "name", "version", "dataDirectory", "plugins", c_ToneMappingKey }
				};
				for (const std::string_view known : c_KnownKeys) json.erase(std::string(known));
				metadata.unknown = std::move(json);
				return metadata;
			}
			catch (const nlohmann::json::exception& e)
			{
				core::throw_runtime_error("Malformed project file: {}", e.what());
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
			core::throw_runtime_error("Failed to create project directory: {}", root.string());

		for (const auto category : c_RequiredDirectories)
		{
			std::filesystem::create_directories(root / c_DataDirectoryName / category, ec);
			if (ec)
				core::throw_runtime_error("Failed to create data directory: {}", category);
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
		project.m_ToneMapping   = metadata.toneMapping;
		project.m_UnknownKeys   = metadata.unknown;
		if (registry != nullptr)
			project.m_Registry = std::move(registry);

		const auto root = projectFile.parent_path();
		for (const auto category : c_RequiredDirectories)
		{
			auto dir = root / c_DataDirectoryName / category;

			if (std::filesystem::exists(dir))
			{
				if (!std::filesystem::is_directory(dir))
					core::throw_runtime_error(
						"Data directory is not a directory: {}, please consider deleting "
						"manually",
						dir.string());

				continue;
			}

			std::error_code ec;
			std::filesystem::create_directories(root / c_DataDirectoryName / category, ec);

			if (ec)
				core::throw_runtime_error("Failed to create data directory: {}", category);
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
		nlohmann::json json                 = m_UnknownKeys;
		json["name"]                        = m_Name;
		json["version"]                     = m_FormatVersion;
		json["dataDirectory"]               = c_DataDirectoryName;
		json["plugins"]                     = m_PluginIds;
		json[std::string(c_ToneMappingKey)] = std::string(toneMappingName(m_ToneMapping));

		std::ofstream stream(m_ProjectFile, std::ios::binary);
		if (!stream)
			core::throw_runtime_error("Cannot write project file: {}", m_ProjectFile.string());

		stream << doc::canonicalDump(json);
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
