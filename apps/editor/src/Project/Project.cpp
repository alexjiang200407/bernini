#include "Project/Project.h"

#include <nlohmann/json.hpp>

static constexpr std::array<std::string_view, 5> c_RequiredDirectories = {
	"Meshes", "Textures", "textures_src", "Materials", "Levels",
};

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
			throw std::runtime_error("Failed to create data directory: " + std::string(category));
	}

	Project project;
	project.m_name          = std::string(name);
	project.m_projectFile   = projectFile;
	project.m_formatVersion = c_FormatVersion;
	project.Save();

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
	project.m_projectFile   = projectFile;
	project.m_name          = json.value("name", projectFile.stem().string());
	project.m_formatVersion = json.value("version", static_cast<int>(c_FormatVersion));

	const auto root = projectFile.parent_path();
	for (const auto category : c_RequiredDirectories)
	{
		auto dir = root / c_DataDirectoryName / category;

		if (std::filesystem::exists(dir))
		{
			if (!std::filesystem::is_directory(dir))
				throw std::runtime_error(
					std::format(
						"Data directory is not a directory: {}, please consider deleting manually",
						dir.string()));

			continue;
		}

		std::error_code ec;
		std::filesystem::create_directories(root / c_DataDirectoryName / category, ec);

		if (ec)
			throw std::runtime_error("Failed to create data directory: " + std::string(category));
	}

	return project;
}

void
Project::Save() const
{
	const nlohmann::json json = {
		{ "name", m_name },
		{ "version", m_formatVersion },
		{ "dataDirectory", c_DataDirectoryName },
	};

	std::ofstream stream(m_projectFile);
	if (!stream)
		throw std::runtime_error("Cannot write project file: " + m_projectFile.string());

	stream << json.dump(4);
}
