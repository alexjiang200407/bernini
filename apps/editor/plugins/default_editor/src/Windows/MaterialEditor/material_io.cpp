#include "material_io.h"

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/material_graph.h"
#include <algorithm>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMaterial.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/asset_paths.h>
#include <editor_sdk/mesh_load.h>

#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

#include <assetlib/AssetStore.h>
#include <assetlib/material_bake.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>
#include <editor_plugin_api/localize.h>
#include <exception>
#include <filesystem>
#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <system_error>

namespace editor
{
	bool
	IsSameMaterialFile(const QString& a, const QString& b)
	{
		if (a.isEmpty() || b.isEmpty())
			return false;

		const auto normalise = [](const QString& path) {
			const auto source = std::filesystem::path(path.toStdWString());

			std::error_code ec;
			const auto      resolved = std::filesystem::weakly_canonical(source, ec);

			return QString::fromStdWString(
				ec ? source.lexically_normal().wstring() : resolved.wstring());
		};

		return normalise(a).compare(normalise(b), Qt::CaseInsensitive) == 0;
	}

	assetlib::BMaterial
	BuildMaterial(
		MaterialGraphModel&         model,
		const QString&              materialPath,
		const assetlib::AssetStore& store)
	{
		assetlib::BMaterial material =
			CompileMaterial(model, QFileInfo(materialPath).completeBaseName(), store.GetDataRoot());

		const auto file = std::filesystem::path(materialPath.toStdWString());
		if (std::filesystem::exists(file))
		{
			try
			{
				const assetlib::BMaterial existing =
					store.Load<assetlib::BMaterial>(store.KeyFor(file));
				material.pbr.baseColorTexture = existing.pbr.baseColorTexture;
				material.pbr.normalTexture    = existing.pbr.normalTexture;
				material.pbr.ormTexture       = existing.pbr.ormTexture;
				material.pbr.routeStamps      = existing.pbr.routeStamps;
				material.pbr.bakeToken        = existing.pbr.bakeToken;

				// The occlusion map's bake output and stamp are the bake's, not the board's, exactly
				// as the triplet's: a rewired authored map's stale stamp is what reports it. A board
				// that dropped the map drops its bake here too, since a baked map with no source
				// beside it is the stripped shape and reads as current.
				if (!material.pbr.geometryOcclusionTexture.empty())
				{
					material.pbr.geometryOcclusionBakedTexture =
						existing.pbr.geometryOcclusionBakedTexture;
					material.pbr.geometryOcclusionStamp = existing.pbr.geometryOcclusionStamp;
				}

				// The surface twin of the triplet lines above: the board authors the routes, the
				// bake owns the stamps and the map. A rewired slot's stale stamps are what report
				// the bake stale, exactly as a rerouted PBR channel's do.
				for (assetlib::SurfaceTextureBinding& slot : material.surface.textures)
				{
					const auto was = std::ranges::find_if(
						existing.surface.textures,
						[&](const assetlib::SurfaceTextureBinding& before) {
							return before.name == slot.name;
						});
					if (was == existing.surface.textures.end())
						continue;

					slot.routeStamps = was->routeStamps;
					slot.bakedPath   = was->bakedPath;
					slot.bakeToken   = was->bakeToken;
				}

				// Document keys this build does not know ride through a save untouched -- a
				// sibling branch's field must survive this editor's round-trip.
				material.extraJson = existing.extraJson;
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialEditor: could not read the existing material: %s", e.what());
			}
		}

		return material;
	}

	QString
	DefaultMaterialPath(const std::filesystem::path& dataRoot, const QString& name)
	{
		if (dataRoot.empty())
			return name;

		auto dir = dataRoot / assetlib::c_MaterialsDirectoryName;

		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec))
			dir = dataRoot;

		return QString::fromStdWString((dir / name.toStdWString()).wstring());
	}

	QString
	AutoSaveMaterialPath(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& meshPath,
		const QString&               submeshName)
	{
		const QString stem = ToPlainFileStem(submeshName);
		const QString file =
			QStringLiteral("%1.bmaterial").arg(stem.isEmpty() ? QStringLiteral("material") : stem);

		if (meshPath.empty())
			return DefaultMaterialPath(dataRoot, file);

		const QString mesh = QString::fromStdWString(meshPath.stem().wstring());
		return DefaultMaterialPath(dataRoot, QStringLiteral("%1/%2").arg(mesh, file));
	}

	QStringList
	HeldOpenByMaterialEditor(const QStringList& materials, const std::filesystem::path& previewMesh)
	{
		auto held = materials;
		if (!previewMesh.empty())
			held << QString::fromStdWString(previewMesh.wstring());
		return held;
	}

	QStringList
	UniqueMaterialFiles(const QStringList& paths)
	{
		auto unique = QStringList();
		unique.reserve(paths.size());

		for (const QString& path : paths)
		{
			if (path.isEmpty())
				continue;

			const auto names = [&path](const QString& kept) {
				return IsSameMaterialFile(kept, path);
			};

			if (!std::ranges::any_of(unique, names))
				unique << path;
		}

		return unique;
	}

	QString
	MaterialSaveSummary(const editor::ILanguageResolver& language, const MaterialSaveResult& result)
	{
		if (result.failed.isEmpty() && result.unattached.isEmpty())
			return {};

		auto lines = QStringList();

		if (result.saved == 1)
			lines << editor::Localize(
				language,
				"bernini.material.save_summary_saved_one",
				{ result.saved },
				"Saved {0} material.");
		else if (result.saved > 1)
			lines << editor::Localize(
				language,
				"bernini.material.save_summary_saved_many",
				{ result.saved },
				"Saved {0} materials.");

		if (!result.failed.isEmpty())
			lines << editor::Localize(
				language,
				"bernini.material.save_summary_failed",
				{ result.failed.join(QLatin1Char('\n')) },
				"Could not write:\n{0}");

		if (!result.unattached.isEmpty())
		{
			lines << editor::Localize(
				language,
				"bernini.material.save_summary_unattached",
				{ result.unattached.join(QLatin1Char('\n')) },
				"Written, but the mesh could not be made to name them:\n{0}");
		}

		return lines.join(QStringLiteral("\n\n"));
	}

	bool
	GenerateTangents(
		const editor::ILanguageResolver& language,
		QWidget*                         parent,
		const assetlib::AssetStore&      store,
		const std::filesystem::path&     meshPath)
	{
		if (meshPath.empty())
			return false;

		const QString title = editor::Localize(
			language,
			"bernini.material.generate_tangents_button",
			"Generate Tangents");

		auto confirm = QMessageBox(parent);
		confirm.setWindowTitle(title);
		confirm.setIcon(QMessageBox::Question);
		confirm.setText(
			editor::Localize(
				language,
				"bernini.material.generate_tangents_confirm_text",
				{ QString::fromStdString(meshPath.filename().string()) },
				"Derive tangents for '{0}'?"));
		confirm.setInformativeText(
			editor::Localize(
				language,
				"bernini.material.generate_tangents_confirm_informative",
				"Every submesh that has none gains one, and the mesh is rewritten and "
				"reloaded. Unsaved graph edits are lost, and a submesh that already has "
				"tangents keeps the authored ones."));

		auto* run = confirm.addButton(
			editor::Localize(
				language,
				"bernini.material.generate_tangents_confirm_button",
				"Generate"),
			QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(run);
		confirm.exec();

		if (confirm.clickedButton() != run)
			return false;

		auto result = assetlib::TangentGenResult();

		// Reading, deriving and writing a whole mesh is not instant, and none of it touches bgl.
		const background::TaskResult done =
			background::RunWithLoadingScreen(parent, title, [&](background::Progress& progress) {
				progress.Report(
					0,
					0,
					editor::Localize(
						language,
						"bernini.material.generate_tangents_progress",
						"Deriving tangents..."));

				// Through the seam, so a stale mesh regenerates first rather than refusing --
				// and a regeneration already derives tangents, which then reports as nothing
				// left to generate.
				assetlib::BMesh mesh = LoadMeshThroughSeam(store, meshPath);
				result               = assetlib::generateTangents(mesh);

				if (result.generated > 0)
				{
					store.Save(mesh, store.KeyFor(meshPath));
				}
			});

		if (!done.Completed())
		{
			QMessageBox::warning(
				parent,
				title,
				editor::Localize(
					language,
					"bernini.material.generate_tangents_failed",
					{ done.error },
					"Could not rewrite the mesh:\n\n{0}"));
			return false;
		}

		if (result.generated == 0)
		{
			QMessageBox::information(
				parent,
				title,
				editor::Localize(
					language,
					"bernini.material.generate_tangents_nothing_to_do",
					{ result.kept, result.skipped },
					"Nothing to do: {0} submeshes already have tangents, and {1} cannot have "
					"one derived (no normals, no UVs, or no triangles)."));
			return false;
		}

		return true;
	}
}
