#include "import_writers.h"
#include <assetlib/bmesh.h>

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/material_graph.h"

#include <assetlib/AssetStore.h>

namespace editor
{
	void
	WriteImportedMaterials(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir,
		std::span<const QString>          stems)
	{
		namespace fs = std::filesystem;

		// The stems were chosen against a material table probed before the dialog opened; this one comes
		// from a second parse of the same file after it closed. A source re-exported while the dialog sat
		// open has a different table, and stems taken from the old one would name files after materials
		// that are no longer at those indices.
		if (stems.size() != imported.materials.size())
		{
			throw std::runtime_error(
				"this file's materials changed while the import dialog was open; import it again");
		}

		// No device: the graph is authored, not drawn, and a TextureNode takes a null scene on purpose.
		const auto registry = MakeMaterialNodeRegistry(nullptr, nullptr);

		const std::vector<std::string> textureNames = assetlib::importedTextureFileNames(imported);

		const auto texturePath = [&](uint32_t index) {
			return index >= textureNames.size() ?
			           QString() :
			           QString::fromStdWString((textureDir / textureNames[index]).wstring());
		};

		auto relative = std::vector<std::string>(imported.materials.size());

		for (size_t i = 0; i < imported.materials.size(); ++i)
		{
			const assetlib::imp::BMaterialImport& source = imported.materials[i];

			// A material whose shading model the engine has no payload for is left behind rather than
			// stamped into a PBR one it never was, and carries no stem to be written under.
			if (!source.isPbr || stems[i].isEmpty())
				continue;

			const QString& stem = stems[i];
			const fs::path file = materialDir / (stem + ".bmaterial").toStdWString();

			MaterialGraphModel model(registry);
			BuildImportedMaterialGraph(
				model,
				source,
				ImportedMaterialMaps{ texturePath(source.baseColorTexture),
			                          texturePath(source.normalTexture),
			                          texturePath(source.ormTexture),
			                          texturePath(source.occlusionTexture) });

			const assetlib::AssetStore store(dataRoot);
			store.Save(CompileMaterial(model, stem, dataRoot), store.KeyFor(file));

			relative[i] =
				Rebase(QString::fromStdWString(file.wstring()), dataRoot, true).toStdString();
		}

		// Only once every file is on disk: a `.bmesh` naming a material that does not exist is what
		// gamelib's AcquireMaterial throws on, and is the reference an import must never make.
		for (size_t i = 0; i < imported.submeshes.size(); ++i)
		{
			const uint32_t index = imported.submeshes[i].material;
			if (index >= relative.size() || relative[index].empty())
				continue;

			assetlib::attachMaterial(mesh, static_cast<uint32_t>(i), relative[index]);
		}
	}
}
