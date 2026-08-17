#include "import_writers.h"

#include "Project/Project.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/material_graph.h"

#include <QStringList>

#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/container_format.h>
#include <assetlib/skeleton.h>

namespace editor::import
{
	void
	WriteMaterials(
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

		fs::create_directories(materialDir);

		// No device: the graph is authored, not drawn, and a TextureNode takes a null scene on purpose.
		const auto registry = MakeMaterialNodeRegistry(nullptr, nullptr);

		const auto texturePath = [&](uint32_t index) {
			return index == assetlib::c_InvalidIndex ?
			           QString() :
			           QString::fromStdWString(
						   (textureDir / assetlib::textureFileName(index)).wstring());
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
			                          texturePath(source.ormTexture) });

			assetlib::saveMaterial(CompileMaterial(model, stem, dataRoot), file);

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

	void
	WriteRig(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      bskelPath,
		const std::filesystem::path&      banimPath,
		bool                              writeClips)
	{
		if (imported.skeleton.bones.empty())
			return;

		// A project scaffolded before these categories existed has neither directory.
		std::filesystem::create_directories(bskelPath.parent_path());

		assetlib::saveSkeleton(imported.skeleton, bskelPath);
		mesh.skeleton =
			Rebase(QString::fromStdWString(bskelPath.wstring()), dataRoot, true).toStdString();

		if (!writeClips || imported.animations.clips.empty())
			return;

		// The clip set names the rig by the same path the mesh does, so all three agree on which file
		// the joint indices are addressed against.
		std::filesystem::create_directories(banimPath.parent_path());

		assetlib::AnimationSet clips = imported.animations;
		clips.skeleton               = mesh.skeleton;
		assetlib::saveAnimations(clips, banimPath);
	}

	void
	WriteMesh(const assetlib::BMesh& mesh, const std::filesystem::path& bmeshPath)
	{
		std::filesystem::create_directories(bmeshPath.parent_path());
		assetlib::save(mesh, bmeshPath);
	}

	std::filesystem::path
	FindMatchingSkeleton(const std::filesystem::path& dataRoot, const assetlib::Skeleton& skeleton)
	{
		namespace fs = std::filesystem;

		const fs::path root = dataRoot / Project::c_SkeletonsDirectoryName;

		std::error_code ec;
		if (!fs::exists(root, ec))
			return {};

		const uint64_t wanted = assetlib::skeletonSignature(skeleton);

		auto       matches = std::vector<fs::path>();
		const auto walk    = fs::directory_options::skip_permission_denied;

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, walk, ec))
		{
			if (!entry.is_regular_file(ec) ||
			    entry.path().extension() != assetlib::c_SkeletonExtension)
				continue;

			try
			{
				if (assetlib::skeletonSignature(assetlib::loadSkeleton(entry.path())) == wanted)
					matches.push_back(entry.path());
			}
			catch (const std::exception&)
			{
				// A `.bskel` that will not load is not the rig we are looking for; the asset scan is
				// where a broken one gets reported.
			}
		}

		if (matches.empty())
			return {};

		// Directory order is unspecified, so choosing one would make the reference written into the
		// `.banim` depend on the filesystem -- and would scatter one rig's clips across two skeletons,
		// which is the thing a VAT bake cannot then fit one bounding box around.
		if (matches.size() > 1)
		{
			auto named = QStringList();
			for (const fs::path& match : matches)
				named << Rebase(QString::fromStdWString(match.wstring()), dataRoot, true);

			throw std::runtime_error(
				QString(
					"this project holds %1 skeletons with the same signature, so which one these "
					"clips belong to is ambiguous: %2")
					.arg(matches.size())
					.arg(named.join(", "))
					.toStdString());
		}

		return matches.front();
	}

	void
	WriteClips(
		const assetlib::imp::BMeshImport& imported,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      banimPath)
	{
		if (imported.animations.clips.empty())
			throw std::runtime_error("this file carries no animation to import");

		// The clips are per-bone samples addressed by index, so without the rig they were authored
		// against there is nothing to say which bone each one drives.
		if (imported.skeleton.bones.empty())
			throw std::runtime_error("this file carries no rig, so its clips address nothing");

		const std::filesystem::path rig = FindMatchingSkeleton(dataRoot, imported.skeleton);
		if (rig.empty())
		{
			throw std::runtime_error(
				"no skeleton in this project matches this file's rig. Import one of these "
				"files with the mesh turned on first, which writes the rig these clips "
				"attach to.");
		}

		std::filesystem::create_directories(banimPath.parent_path());

		assetlib::AnimationSet clips = imported.animations;
		clips.skeleton =
			Rebase(QString::fromStdWString(rig.wstring()), dataRoot, true).toStdString();
		assetlib::saveAnimations(clips, banimPath);
	}

	void
	RollBack(std::span<const WrittenFile> files, std::span<const WrittenDir> dirs)
	{
		namespace fs = std::filesystem;

		std::error_code ec;

		for (const WrittenFile& file : files)
			if (!file.existed)
				fs::remove(file.path, ec);

		for (const WrittenDir& dir : dirs)
		{
			// remove_all is recursive, so the folder it is handed had better be the one this import made.
			// An empty path means the import wrote no such folder; a path naming the category root itself
			// would mean the import's subdirectory got lost somewhere, and taking the root down with it is
			// not a recovery.
			if (dir.existed || dir.path.empty() ||
			    dir.path.filename() == fs::path(dir.categoryRoot))
				continue;

			fs::remove_all(dir.path, ec);
		}
	}
}
