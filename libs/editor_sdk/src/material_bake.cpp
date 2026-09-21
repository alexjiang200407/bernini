#include <QFileInfo>
#include <QString>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BMaterial.h>
#include <editor_sdk/material_bake.h>
#include <string>
namespace editor
{
	void
	BakeMaterials(
		const assetlib::AssetStore& store,
		const QStringList&          materials,
		background::Progress&       progress)
	{
		int done = 0;
		for (const QString& relative : materials)
		{
			progress.Report(
				done,
				static_cast<int>(materials.size()),
				QStringLiteral("Baking %1...").arg(QFileInfo(relative).fileName()));

			const std::string key = relative.toStdString();

			assetlib::BMaterial material = store.Load<assetlib::BMaterial>(key);
			store.BakeMaterial(material, progress.Cancellation());
			store.Save(material, key);

			++done;
		}
	}

}
