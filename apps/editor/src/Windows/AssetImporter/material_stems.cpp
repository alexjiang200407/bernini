#include "Windows/AssetImporter/material_stems.h"

#include "util/asset_paths.h"

#include <QSet>

namespace editor
{
	namespace
	{
		QString
		UniqueStem(std::string_view name, size_t index, QSet<QString>& taken)
		{
			QString stem = ToPlainFileStem(
				QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));

			if (stem.isEmpty())
				stem = QStringLiteral("material%1").arg(index);

			QString unique = stem;
			for (int n = 2; taken.contains(unique.toLower()); ++n)
				unique = QStringLiteral("%1_%2").arg(stem).arg(n);

			taken.insert(unique.toLower());
			return unique;
		}
	}

	QStringList
	MaterialStems(std::span<const assetlib::GltfMaterial> materials)
	{
		auto taken = QSet<QString>();
		auto stems = QStringList();
		stems.reserve(static_cast<qsizetype>(materials.size()));

		for (size_t i = 0; i < materials.size(); ++i)
		{
			// A non-PBR material claims no stem at all, so it cannot take a name off one that will
			// actually be written.
			stems << (materials[i].isPbr ? UniqueStem(materials[i].name, i, taken) : QString());
		}

		return stems;
	}
}
