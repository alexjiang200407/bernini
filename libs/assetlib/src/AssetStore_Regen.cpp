#include <assetlib/AssetStore.h>
#include <assetlib/regen/RegenMesh.h>

#include <assetlib/asset_import.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/container_format.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>

#include "MountedFileReader.h"
#include "bake_tokens.h"
#include "cache_io.h"
#include "import_bounds.h"
#include "mounted_io.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		/** One entry's cache key against the project it sits in, and the document that keyed it. */
		struct CheckedKey
		{
			cache::PeekedKey              key;
			std::optional<ImportDocument> document;
			bool                          stale = false;
		};

		CheckedKey
		checkKey(
			const AssetStore& store,
			std::string_view  path,
			uint32_t          magic,
			uint64_t          bakeToken,
			std::string_view  what)
		{
			MountedFileReader reader(store.GetFiles(), path, what);
			CheckedKey        checked{ cache::peekKey(reader, magic, what), {}, false };
			checked.stale = checked.key.bakeToken != bakeToken;
			if (checked.key.source.key.empty())
				return checked;

			const std::string documentKey = importDocumentKeyFor(checked.key.source.key);
			if (store.GetFiles().Exists(documentKey))
			{
				checked.document = loadImportDocument(store.GetFiles(), documentKey);
				if (parametersHashOf(*checked.document) != checked.key.source.parametersHash)
					checked.stale = true;
			}

			// An absent source cannot be compared, so it stales nothing: the entry stays current
			// while its token holds, which is what keeps a project missing its sources loadable.
			const SourceStamp stamp = stampOf(store.GetFiles(), checked.key.source.key);
			if (stamp != SourceStamp() && stamp != checked.key.source.stamp)
				checked.stale = true;

			return checked;
		}

		/** A stale entry's source, re-imported in memory, with the reference that now keys it. */
		struct RegeneratedGroup
		{
			imp::BMeshImport              import;
			SourceRef                     ref;
			std::optional<ImportDocument> document;
		};

		RegeneratedGroup
		regenerate(const AssetStore& store, CheckedKey&& checked, std::string_view what)
		{
			const SourceRef& source = checked.key.source;
			core::throw_runtime_error_if(
				source.key.empty(),
				"{}: written at another bake revision and no source was ever recorded, so it "
				"cannot be regenerated; re-import it",
				what);
			core::throw_runtime_error_if(
				!store.Exists(source.key),
				"{}: stale, and its source '{}' is not in the project to regenerate from",
				what,
				source.key);

			// Regenerating at default parameters where the lost document said otherwise is
			// #413's exact shape, so a missing document refuses rather than guesses.
			core::throw_runtime_error_if(
				!checked.document.has_value(),
				"{}: stale, and the import document beside '{}' is gone, so the parameters it "
				"should regenerate at are unknowable; re-import the source",
				what,
				source.key);

			// The copied source lives only on the loose layer -- pack excludes it -- and the glTF
			// parser reads a file, so this is a read that must address the host.
			RegeneratedGroup group{
				loadFromGltf(store.ResolveWritePath(source.key), {}, checked.document->sampleRate),
				SourceRef(),
				std::move(checked.document),
			};
			group.ref.key            = source.key;
			group.ref.stamp          = stampOf(store.GetFiles(), source.key);
			group.ref.parametersHash = parametersHashOf(*group.document);
			return group;
		}

		/** The `.bskel` whose header names `sourceKey` as its own source, or empty. */
		std::string
		groupSkeletonKey(const AssetStore& store, std::string_view sourceKey)
		{
			for (const std::string& path : store.GetFiles().Enumerate(c_SkeletonsDirectoryName))
			{
				if (extensionOf(path) != c_SkeletonExtension)
					continue;
				try
				{
					MountedFileReader reader(store.GetFiles(), path, "bskel");
					if (cache::peekKey(reader, magic::c_BSkel, "bskel").source.key == sourceKey)
						return path;
				}
				catch (const std::exception&)
				{
					// A rig whose header will not read is not this group's; the asset scan is
					// where a broken one gets reported.
				}
			}
			return {};
		}
	}

	RegenMesh
	AssetStore::LoadRegenMesh(std::string_view path) const
	{
		if (IsReadOnly())
			return { load(*m_Files, path), {} };

		CheckedKey checked = checkKey(*this, path, magic::c_BMesh, c_BMeshBakeToken, "bmesh");
		if (!checked.stale)
		{
			RegenMesh current{ load(*m_Files, path), {} };
			if (checked.document)
				current.unboundBindings = applyBindings(current.mesh, checked.document->bindings);
			return current;
		}

		RegeneratedGroup group = regenerate(*this, std::move(checked), "bmesh");
		core::throw_runtime_error_if(
			group.import.meshes.empty(),
			"'{}': its re-exported source no longer carries a mesh; restore it in the DCC, or "
			"delete this file",
			path);

		RegenMesh current{ toBMesh(group.import), {} };
		generateTangents(current.mesh);
		requireUniqueSubmeshNames(current.mesh);
		current.mesh.source = group.ref;
		if (isSkinned(current.mesh))
		{
			current.mesh.skeleton = groupSkeletonKey(*this, group.ref.key);
			core::throw_runtime_error_if(
				current.mesh.skeleton.empty(),
				"'{}': its regenerated source carries a rig but the project holds no .bskel of "
				"that source; re-import the source",
				path);
		}
		current.unboundBindings = applyBindings(current.mesh, group.document->bindings);
		return current;
	}

	Skeleton
	AssetStore::LoadRegenSkeleton(std::string_view path) const
	{
		if (IsReadOnly())
			return loadSkeleton(*m_Files, path);

		CheckedKey checked = checkKey(*this, path, magic::c_BSkel, c_BSkelBakeToken, "bskel");
		if (!checked.stale)
			return loadSkeleton(*m_Files, path);

		RegeneratedGroup group = regenerate(*this, std::move(checked), "bskel");
		core::throw_runtime_error_if(
			group.import.skeleton.bones.empty(),
			"'{}': its re-exported source no longer carries a rig; restore the skeleton in the "
			"DCC, or delete this file and its dependents",
			path);

		Skeleton skeleton = group.import.skeleton;
		skeleton.source   = group.ref;
		return skeleton;
	}

	AnimationSet
	AssetStore::LoadRegenAnimations(std::string_view path) const
	{
		if (IsReadOnly())
			return loadAnimations(*m_Files, path);

		CheckedKey checked = checkKey(*this, path, magic::c_BAnim, c_BAnimBakeToken, "banim");
		if (!checked.stale)
			return loadAnimations(*m_Files, path);

		RegeneratedGroup group = regenerate(*this, std::move(checked), "banim");
		core::throw_runtime_error_if(
			group.import.animations.clips.empty(),
			"'{}': its re-exported source no longer carries clips; restore them in the DCC, or "
			"delete this file",
			path);
		core::throw_runtime_error_if(
			group.import.skeleton.bones.empty(),
			"'{}': its re-exported source no longer carries a rig, so its clips address nothing",
			path);

		AnimationSet clips = group.import.animations;
		clips.source       = group.ref;

		std::string rigKey = groupSkeletonKey(*this, group.ref.key);
		if (rigKey.empty())
		{
			// A clips-only group: no output of this source is a rig, so re-resolve by signature,
			// exactly as the import that wrote this file did.
			const std::filesystem::path rig =
				findMatchingSkeleton(GetDataRoot(), group.import.skeleton);
			core::throw_runtime_error_if(
				rig.empty(),
				"'{}': no skeleton in this project matches its clips' rig any more; re-import "
				"the rig first",
				path);
			rigKey = mountKeyFor(GetDataRoot(), rig);
		}
		clips.skeleton = rigKey;

		const Skeleton skeleton = LoadRegenSkeleton(rigKey);
		bakeBoundsForRig(clips, GetDataRoot(), normalizeRef(rigKey), skeleton);

		// The group's own mesh may itself be stale on disk, where the walk above cannot read it;
		// measured from the regenerated form, its box is never the one missing. Tangents first:
		// the box's signature hashes the vertex layout, and every consumer holds the mesh with
		// them generated -- a box keyed to the raw import would never be found.
		BMesh mesh = toBMesh(group.import);
		generateTangents(mesh);
		bakePosedBounds(clips, mesh, skeleton);

		return clips;
	}
}
