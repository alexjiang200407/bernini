#include <assetlib/asset_import.h>

#include <assetlib/banim_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/container_format.h>
#include <assetlib/project_layout.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <core/err/util.h>

#include "ref_paths.h"

namespace assetlib
{
	void
	writeImportedMesh(const BMesh& mesh, const std::filesystem::path& bmeshPath)
	{
		std::filesystem::create_directories(bmeshPath.parent_path());
		save(mesh, bmeshPath);
	}

	void
	writeImportedRig(
		const imp::BMeshImport&      imported,
		BMesh&                       mesh,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& bskelPath,
		const std::filesystem::path& banimPath,
		bool                         writeClips)
	{
		if (imported.skeleton.bones.empty())
			return;

		// A project scaffolded before these categories existed has neither directory.
		std::filesystem::create_directories(bskelPath.parent_path());

		saveSkeleton(imported.skeleton, bskelPath);
		mesh.skeleton = mountKeyFor(dataRoot, bskelPath);

		if (!writeClips || imported.animations.clips.empty())
			return;

		// The clip set names the rig by the same path the mesh does, so all three agree on which file
		// the joint indices are addressed against.
		std::filesystem::create_directories(banimPath.parent_path());

		AnimationSet clips = imported.animations;
		clips.skeleton     = mesh.skeleton;
		bakePosedBounds(clips, mesh, imported.skeleton);
		saveAnimations(clips, banimPath);
	}

	std::filesystem::path
	findMatchingSkeleton(const std::filesystem::path& dataRoot, const Skeleton& skeleton)
	{
		namespace fs = std::filesystem;

		const fs::path root = dataRoot / c_SkeletonsDirectoryName;

		std::error_code ec;
		if (!fs::exists(root, ec))
			return {};

		const uint64_t wanted = skeletonSignature(skeleton);

		auto       matches = std::vector<fs::path>();
		const auto walk    = fs::directory_options::skip_permission_denied;

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, walk, ec))
		{
			if (!entry.is_regular_file(ec) || entry.path().extension() != c_SkeletonExtension)
				continue;

			try
			{
				if (skeletonSignature(loadSkeleton(entry.path())) == wanted)
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
			auto named = std::string();
			for (const fs::path& match : matches)
			{
				if (!named.empty())
					named += ", ";
				named += mountKeyFor(dataRoot, match);
			}

			core::throw_runtime_error(
				"this project holds {} skeletons with the same signature, so which one these clips "
				"belong to is ambiguous: {}",
				matches.size(),
				named);
		}

		return matches.front();
	}

	namespace
	{
		/**
		 * Bakes the posed box of every mesh in the project that skins to `rigRel`, whose skeleton is
		 * `skeleton`. Clips arriving without their geometry have no box to measure otherwise, and a
		 * load that has to measure one is the cost this bake exists to remove.
		 *
		 * By path rather than by signature: findMatchingSkeleton has already refused a project where
		 * two skeletons share a signature, so a mesh reaching this rig by signature names exactly
		 * this file.
		 */
		void
		bakeBoundsForRig(
			AnimationSet&                clips,
			const std::filesystem::path& dataRoot,
			const std::string&           rigRel,
			const Skeleton&              skeleton)
		{
			namespace fs = std::filesystem;

			std::error_code ec;
			const auto      walk = fs::directory_options::skip_permission_denied;
			for (const fs::directory_entry& entry :
			     fs::recursive_directory_iterator(dataRoot, walk, ec))
			{
				if (!entry.is_regular_file(ec) || entry.path().extension() != c_MeshExtension)
					continue;

				try
				{
					if (normalizePath(loadMeshRefs(entry.path()).skeleton) != rigRel)
						continue;

					bakePosedBounds(clips, load(entry.path()), skeleton);
				}
				catch (const std::exception&)
				{
					// A mesh that cannot be read has no box to bake; the asset scan is where a
					// broken one gets reported.
				}
			}
		}
	}

	void
	writeImportedClips(
		const imp::BMeshImport&      imported,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& banimPath)
	{
		if (imported.animations.clips.empty())
			throw std::runtime_error("this file carries no animation to import");

		// The clips are per-bone samples addressed by index, so without the rig they were authored
		// against there is nothing to say which bone each one drives.
		if (imported.skeleton.bones.empty())
			throw std::runtime_error("this file carries no rig, so its clips address nothing");

		const std::filesystem::path rig = findMatchingSkeleton(dataRoot, imported.skeleton);
		if (rig.empty())
		{
			throw std::runtime_error(
				"no skeleton in this project matches this file's rig. Import one of these "
				"files with the mesh turned on first, which writes the rig these clips "
				"attach to.");
		}

		std::filesystem::create_directories(banimPath.parent_path());

		AnimationSet clips = imported.animations;
		clips.skeleton     = mountKeyFor(dataRoot, rig);

		// Measured against the rig the clips will resolve at load, not the imported copy: the two
		// share a signature but a re-authored bind pose deliberately does not change one.
		bakeBoundsForRig(clips, dataRoot, normalizePath(clips.skeleton), loadSkeleton(rig));

		saveAnimations(clips, banimPath);
	}

	void
	rollBackImport(std::span<const ImportedFile> files, std::span<const ImportedDir> dirs)
	{
		namespace fs = std::filesystem;

		std::error_code ec;

		for (const ImportedFile& file : files)
			if (!file.existed)
				fs::remove(file.path, ec);

		for (const ImportedDir& dir : dirs)
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
