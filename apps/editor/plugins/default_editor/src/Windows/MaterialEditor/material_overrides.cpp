#include "Windows/MaterialEditor/material_overrides.h"

#include "Windows/MaterialEditor/material_io.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <assetlib_structs/BMesh.h>
#include <cstdint>
#include <filesystem>
#include <qnamespace.h>
#include <qstringliteral.h>
#include <vector>

namespace editor
{
	std::vector<RegisteredMaterial>
	RegisteredMaterialsFor(const assetlib::BMesh& mesh, const uint32_t sourceSubmesh)
	{
		auto registered = std::vector<RegisteredMaterial>();
		for (const assetlib::SubmeshMaterialOverride& entry : mesh.materialOverrides)
		{
			if (entry.submesh != sourceSubmesh || entry.material >= mesh.materials.size())
				continue;

			registered.emplace_back(
				QString::fromStdString(entry.name),
				QString::fromStdString(mesh.materials[entry.material]));
		}
		return registered;
	}

	bool
	CanRegisterMaterialName(const std::vector<RegisteredMaterial>& taken, const QString& name)
	{
		if (name.trimmed().isEmpty())
			return false;

		return std::ranges::none_of(taken, [&name](const RegisteredMaterial& entry) {
			return entry.name.compare(name, Qt::CaseInsensitive) == 0;
		});
	}

	QString
	NewOverrideMaterialPath(
		const std::filesystem::path& dataRoot,
		const QString&               from,
		const QString&               submeshName,
		const QString&               name)
	{
		const QString stem = from.isEmpty() ? submeshName : QFileInfo(from).completeBaseName();
		const QString file = QStringLiteral("%1_%2.bmaterial").arg(stem, name.trimmed());

		if (from.isEmpty())
			return DefaultMaterialPath(dataRoot, file);

		return QFileInfo(from).dir().filePath(file);
	}
}
