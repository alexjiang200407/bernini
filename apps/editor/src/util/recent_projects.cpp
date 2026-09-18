#include "util/recent_projects.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <core/file/file.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iterator>
#include <qiodevicebase.h>
#include <qlogging.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace editor
{
	namespace
	{
		constexpr auto c_ListFileName = "recent_projects.json";
		constexpr auto c_ProjectsKey  = "projects";

		// One spelling per project, so reopening through a symlinked checkout moves the entry rather
		// than adding a second one.
		std::filesystem::path
		Normalized(const std::filesystem::path& projectFile)
		{
			std::error_code       ec;
			std::filesystem::path canonical = std::filesystem::weakly_canonical(projectFile, ec);
			if (ec)
				return std::filesystem::absolute(projectFile, ec).lexically_normal();
			return canonical;
		}
	}

	std::filesystem::path
	RecentProjectsFileBeside(const std::filesystem::path& configPath)
	{
		return configPath.parent_path() / c_ListFileName;
	}

	std::vector<std::filesystem::path>
	ReadRecentProjects(const std::filesystem::path& listFile)
	{
		QFile file(QString::fromStdWString(listFile.wstring()));
		if (!file.open(QIODeviceBase::ReadOnly))
			return {};

		const QJsonArray entries =
			QJsonDocument::fromJson(file.readAll()).object().value(c_ProjectsKey).toArray();

		auto projects = std::vector<std::filesystem::path>();
		for (const QJsonValue& entry : entries)
		{
			const QString text = entry.toString();
			if (text.isEmpty())
				continue;

			auto            project = std::filesystem::path(text.toStdWString());
			std::error_code ec;
			if (std::filesystem::is_regular_file(project, ec))
				projects.emplace_back(std::move(project));
		}
		return projects;
	}

	void
	RecordRecentProject(
		const std::filesystem::path& listFile,
		const std::filesystem::path& projectFile) noexcept
	{
		try
		{
			const std::filesystem::path recorded = Normalized(projectFile);

			auto projects = ReadRecentProjects(listFile);
			std::erase_if(projects, [&recorded](const std::filesystem::path& p) {
				return Normalized(p) == recorded;
			});
			projects.insert(projects.begin(), recorded);
			if (projects.size() > c_MaxRecentProjects)
				projects.resize(c_MaxRecentProjects);

			auto entries = QJsonArray();
			std::ranges::transform(
				projects,
				std::back_inserter(entries),
				[](const std::filesystem::path& p) {
					return QString::fromStdWString(p.wstring());
				});

			const QByteArray text =
				QJsonDocument(QJsonObject{ { c_ProjectsKey, entries } }).toJson();
			core::file::write_atomic(listFile, std::string_view(text.constData(), text.size()));
		}
		catch (const std::exception& e)
		{
			qWarning("Recent projects: could not record a project: %s", e.what());
		}
	}
}
