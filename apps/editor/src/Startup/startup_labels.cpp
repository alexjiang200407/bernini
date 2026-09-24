#include "Startup/startup_labels.h"
#include "util/editor_language.h"
#include <assetlib/progress.h>
#include <cstddef>
#include <qobject.h>
#include <qtypes.h>
#include <string_view>

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
	}

	QString
	RebuildLabel(const assetlib::ProgressEvent& event)
	{
		// A whole sentence per phase: a translation orders the verb and the file its own way.
		const QString name = FileNameOf(event.subject);
		const bool    none = event.subject.empty();
		switch (event.phase)
		{
		case assetlib::ProgressPhase::kScanning:
			return none ? Localize("editor.startup.checking", "Checking...") :
			              Localize("editor.startup.checking_file", { name }, "Checking {0}");
		case assetlib::ProgressPhase::kRegenerating:
			return none ? Localize("editor.startup.rebuilding", "Rebuilding...") :
			              Localize("editor.startup.rebuilding_file", { name }, "Rebuilding {0}");
		case assetlib::ProgressPhase::kExtractingTextures:
			return none ? Localize("editor.startup.extracting_textures", "Extracting textures...") :
			              Localize(
							  "editor.startup.extracting_textures_file",
							  { name },
							  "Extracting textures {0}");
		case assetlib::ProgressPhase::kBakingMaterials:
			return none ? Localize("editor.startup.baking_material", "Baking material...") :
			              Localize(
							  "editor.startup.baking_material_file",
							  { name },
							  "Baking material {0}");
		case assetlib::ProgressPhase::kResaving:
			return none ? Localize("editor.startup.updating", "Updating...") :
			              Localize("editor.startup.updating_file", { name }, "Updating {0}");
		}
		return none ? Localize("editor.startup.working", "Working on...") :
		              Localize("editor.startup.working_file", { name }, "Working on {0}");
	}
}
