#include "Windows/MaterialEditor/material_overrides.h"

#include "Windows/MaterialEditor/material_graph.h"
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
		// Trimmed on both sides of the comparison, because that is what gets registered: a name
		// typed with a stray space would otherwise pass as new and replace the look it matches.
		const QString wanted = name.trimmed();
		if (wanted.isEmpty())
			return false;

		return std::ranges::none_of(taken, [&wanted](const RegisteredMaterial& entry) {
			return entry.name.trimmed().compare(wanted, Qt::CaseInsensitive) == 0;
		});
	}

	std::vector<RegisteredMaterial>
	LooksBesidesDefault(
		const std::vector<RegisteredMaterial>& registered,
		const QString&                         defaultPath,
		const std::filesystem::path&           dataRoot)
	{
		auto listed = std::vector<RegisteredMaterial>();
		for (const RegisteredMaterial& look : registered)
			if (!IsSameMaterialFile(Rebase(look.material, dataRoot, false), defaultPath))
				listed.push_back(look);

		return listed;
	}

	QString
	NameForOutgoingDefault(
		const std::vector<RegisteredMaterial>& registered,
		const QString&                         defaultPath,
		const std::filesystem::path&           dataRoot)
	{
		if (defaultPath.isEmpty())
			return {};

		for (const RegisteredMaterial& look : registered)
			if (IsSameMaterialFile(Rebase(look.material, dataRoot, false), defaultPath))
				return {};

		const QString stem = QFileInfo(defaultPath).completeBaseName();

		QString name = stem;
		for (int suffix = 2; !CanRegisterMaterialName(registered, name); ++suffix)
			name = QStringLiteral("%1 %2").arg(stem).arg(suffix);

		return name;
	}

	QString
	NewOverrideMaterialPath(
		const std::filesystem::path& dataRoot,
		const QString&               from,
		const QString&               name)
	{
		const QString base = name.trimmed();

		const auto at = [&](const QString& file) {
			return from.isEmpty() ? DefaultMaterialPath(dataRoot, file) :
			                        QFileInfo(from).dir().filePath(file);
		};

		// Two submeshes showing one material and given one override name compute one destination,
		// and the second copy would overwrite the first submesh's look with its own content. So the
		// first free spelling wins instead.
		QString file = base + QStringLiteral(".bmaterial");
		for (int suffix = 2; QFileInfo::exists(at(file)); ++suffix)
			file = QStringLiteral("%1_%2.bmaterial").arg(base).arg(suffix);

		return at(file);
	}
}
