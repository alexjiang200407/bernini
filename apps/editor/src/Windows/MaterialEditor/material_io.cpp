#include "material_io.h"

#include "Async/BackgroundTask.h"
#include "Mesh/mesh_load.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/material_graph.h"
#include <assetlib/Project.h>

#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>

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

	QString
	BakedTexturesSummary(const assetlib::BMaterial& material)
	{
		// A baked triplet is a PBR notion, and a material carries one only once it has been baked -- so a
		// never-baked or non-PBR material has nothing to list. A kLoose material keeps the triplet of its
		// last bake, which is still worth showing: "current baked textures, if any".
		if (material.shadingModel != assetlib::ShadingModel::kPbr)
			return {};

		const assetlib::PbrParams& pbr = material.pbr;
		if (pbr.baseColorTexture.empty() && pbr.normalTexture.empty() && pbr.ormTexture.empty())
			return {};

		const auto line = [](const char* label, const std::string& path) {
			return QStringLiteral("%1: %2").arg(
				QLatin1String(label),
				path.empty() ? QStringLiteral("—") : QString::fromStdString(path));
		};

		return QStringLiteral("Baked textures\n%1\n%2\n%3")
		    .arg(
				line("Base color", pbr.baseColorTexture),
				line("Normal", pbr.normalTexture),
				line("ORM", pbr.ormTexture));
	}

	assetlib::BMaterial
	BuildMaterial(
		MaterialGraphModel&          model,
		const QString&               materialPath,
		const std::filesystem::path& dataRoot)
	{
		assetlib::BMaterial material =
			CompileMaterial(model, QFileInfo(materialPath).completeBaseName(), dataRoot);

		const auto file = std::filesystem::path(materialPath.toStdWString());
		if (std::filesystem::exists(file))
		{
			try
			{
				const assetlib::BMaterial existing = assetlib::loadMaterial(file);
				material.pbr.baseColorTexture      = existing.pbr.baseColorTexture;
				material.pbr.normalTexture         = existing.pbr.normalTexture;
				material.pbr.ormTexture            = existing.pbr.ormTexture;
				material.pbr.routeStamps           = existing.pbr.routeStamps;

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
	MaterialSaveSummary(const MaterialSaveResult& result)
	{
		if (result.unsaved == 0 && result.failed.isEmpty() && result.unattached.isEmpty())
			return {};

		const auto count = [](const int n, const char* one, const char* many) {
			return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
		};

		auto lines = QStringList();

		if (result.saved > 0)
			lines << QStringLiteral("Saved %1.").arg(count(result.saved, "material", "materials"));

		if (result.unsaved > 0)
		{
			lines << QStringLiteral(
						 "Skipped %1 with no material file yet; Save As gives one a file.")
						 .arg(count(result.unsaved, "submesh", "submeshes"));
		}

		if (!result.failed.isEmpty())
			lines << QStringLiteral("Could not write:\n%1")
						 .arg(result.failed.join(QLatin1Char('\n')));

		if (!result.unattached.isEmpty())
		{
			lines << QStringLiteral("Written, but the mesh could not be made to name them:\n%1")
						 .arg(result.unattached.join(QLatin1Char('\n')));
		}

		return lines.join(QStringLiteral("\n\n"));
	}

	void
	BakeMaterials(
		const std::filesystem::path& dataRoot,
		const QStringList&           materials,
		background::Progress&        progress)
	{
		auto desc     = assetlib::MaterialBakeDesc();
		desc.dataRoot = dataRoot;

		int done = 0;
		for (const QString& relative : materials)
		{
			progress.Report(
				done,
				static_cast<int>(materials.size()),
				QStringLiteral("Baking %1...").arg(QFileInfo(relative).fileName()));

			const std::filesystem::path file = dataRoot / relative.toStdWString();

			assetlib::BMaterial material = assetlib::loadMaterial(file);
			assetlib::bakeMaterial(material, desc, progress.Cancellation());
			assetlib::saveMaterial(material, file);

			++done;
		}
	}

	bool
	GenerateTangents(
		QWidget*                     parent,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& meshPath)
	{
		if (meshPath.empty())
			return false;

		auto confirm = QMessageBox(parent);
		confirm.setWindowTitle(QStringLiteral("Generate Tangents"));
		confirm.setIcon(QMessageBox::Question);
		confirm.setText(QStringLiteral("Derive tangents for '%1'?")
		                    .arg(QString::fromStdString(meshPath.filename().string())));
		confirm.setInformativeText(QStringLiteral(
			"Every submesh that has none gains one, and the mesh is rewritten and "
			"reloaded. Unsaved graph edits are lost, and a submesh that already has "
			"tangents keeps the authored ones."));

		auto* run = confirm.addButton(QStringLiteral("Generate"), QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(run);
		confirm.exec();

		if (confirm.clickedButton() != run)
			return false;

		auto result = assetlib::TangentGenResult();

		// Reading, deriving and writing a whole mesh is not instant, and none of it touches bgl.
		const background::TaskResult done = background::RunWithLoadingScreen(
			parent,
			QStringLiteral("Generate Tangents"),
			[&](background::Progress& progress) {
				progress.Report(0, 0, "Deriving tangents...");

				// Through the seam, so a stale mesh regenerates first rather than refusing --
				// and a regeneration already derives tangents, which then reports as nothing
				// left to generate.
				assetlib::BMesh mesh = LoadMeshThroughSeam(dataRoot, meshPath);
				result               = assetlib::generateTangents(mesh);

				if (result.generated > 0)
					assetlib::save(mesh, meshPath);
			});

		if (!done.Completed())
		{
			QMessageBox::warning(
				parent,
				QStringLiteral("Generate Tangents"),
				QStringLiteral("Could not rewrite the mesh:\n\n%1").arg(done.error));
			return false;
		}

		if (result.generated == 0)
		{
			QMessageBox::information(
				parent,
				QStringLiteral("Generate Tangents"),
				QStringLiteral(
					"Nothing to do: %1 submeshes already have tangents, and %2 cannot have "
					"one derived (no normals, no UVs, or no triangles).")
					.arg(result.kept)
					.arg(result.skipped));
			return false;
		}

		return true;
	}
}
