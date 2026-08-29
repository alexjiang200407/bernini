#include "Startup/startup_labels.h"

namespace editor::startup
{
	namespace
	{
		QString
		FileNameOf(std::string_view key)
		{
			const size_t           slash = key.rfind('/');
			const std::string_view name =
				slash == std::string_view::npos ? key : key.substr(slash + 1);
			return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
		}

		QString
		PhaseVerb(assetlib::ProgressPhase phase)
		{
			switch (phase)
			{
			case assetlib::ProgressPhase::kScanning:
				return QStringLiteral("Checking");
			case assetlib::ProgressPhase::kRegenerating:
				return QStringLiteral("Rebuilding");
			case assetlib::ProgressPhase::kExtractingTextures:
				return QStringLiteral("Extracting textures");
			case assetlib::ProgressPhase::kBakingMaterials:
				return QStringLiteral("Baking material");
			case assetlib::ProgressPhase::kResaving:
				return QStringLiteral("Updating");
			}
			return QStringLiteral("Working on");
		}
	}

	QString
	RebuildLabel(const assetlib::ProgressEvent& event)
	{
		const QString verb = PhaseVerb(event.phase);
		if (event.subject.empty())
			return verb + QStringLiteral("...");

		return QStringLiteral("%1 %2").arg(verb, FileNameOf(event.subject));
	}
}
