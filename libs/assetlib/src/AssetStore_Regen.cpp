#include <assetlib/AssetStore.h>
#include <assetlib/MeshBindings.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/ResolvedImport.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>

#include <assetlib/asset_import.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceStamp.h>
#include <assetlib_structs/magic.h>
#include <concepts>
#include <core/err/util.h>
#include <mutex>

#include "MountedFileReader.h"
#include "cache_io.h"
#include "import_bounds.h"
#include "mounted_io.h"
#include "plant_bake.h"
#include "ref_paths.h"
#include "regen_group.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

		MeshBindings
		bindingSnapshot(
			const BMesh&              mesh,
			const ImportDocument&     document,
			std::vector<std::string>& unbound)
		{
			auto result     = MeshBindings();
			result.skeleton = document.skeleton;
			result.submeshMaterials.resize(mesh.submeshes.size());
			result.grassLooks.resize(mesh.grassFields.fields.size());
			auto submeshes = std::unordered_map<std::string_view, uint32_t>();
			for (uint32_t i = 0; i < mesh.submeshes.size(); ++i)
				submeshes.emplace(mesh.stringPool.at(mesh.submeshes[i].nameOffset), i);
			auto fields = std::unordered_map<std::string_view, uint32_t>();
			for (uint32_t i = 0; i < mesh.grassFields.fields.size(); ++i)
				fields.emplace(mesh.grassFields.fields[i].name, i);
			auto       missing       = std::unordered_set<std::string_view>();
			const auto reportMissing = [&](std::string_view name) {
				if (missing.insert(name).second)
					unbound.emplace_back(name);
			};
			for (const auto& binding : document.bindings)
			{
				const bool  grass = isGrassBinding(binding);
				const auto& names = grass ? fields : submeshes;
				if (const auto found = names.find(binding.submesh); found != names.end())
				{
					auto& materials          = grass ? result.grassLooks : result.submeshMaterials;
					materials[found->second] = binding.material;
				}
				else
					reportMissing(binding.submesh);
			}
			for (const auto& entry : document.materialOverrides)
				if (const auto found = submeshes.find(entry.submesh); found != submeshes.end())
					result.materialOverrides.push_back(
						{ found->second, entry.name, entry.material });
				else
					reportMissing(entry.submesh);
			std::ranges::sort(result.materialOverrides, [](const auto& a, const auto& b) {
				return std::tie(a.submesh, a.name) < std::tie(b.submesh, b.name);
			});
			return result;
		}

		CheckedKey
		checkKey(
			const AssetStore& store,
			std::string_view  path,
			uint32_t          magic,
			uint64_t          bakeToken,
			std::string_view  what,
			bool              compareSource = true)
		{
			MountedFileReader reader(store.GetFiles(), path, what);
			CheckedKey        checked{ cache::peekKey(reader, magic, what), {}, false };
			checked.stale = checked.key.bakeToken != bakeToken;
			if (checked.key.source.key.empty())
				return checked;

			std::string documentKey = importDocumentKeyFor(checked.key.source.key);
			if (store.GetFiles().Exists(documentKey))
			{
				checked.document = loadImportDocument(store.GetFiles(), documentKey);
				if (checked.document->identity.id != 0 &&
				    std::ranges::find(checked.document->outputs, path) ==
				        checked.document->outputs.end())
					checked.document.reset();
			}
			if (!checked.document)
				if (auto owner = store.FindImportForOutput(path))
				{
					documentKey      = std::move(owner->documentKey);
					checked.document = std::move(owner->document);
				}
			if (checked.document)
			{
				checked.key.source.key = importedSourceKeyFor(documentKey, *checked.document);
				if (importDocumentKeyFor(checked.key.source.key) != documentKey)
					core::throw_runtime_error(
						"{}: import document is not beside its recorded source",
						documentKey);
				if (parametersHashOf(*checked.document) != checked.key.source.parametersHash)
					checked.stale = true;
			}
			if (store.IsReadOnly())
			{
				if (!checked.document ||
				    importedSourceKeyFor(documentKey, *checked.document) != checked.key.source.key)
					core::throw_runtime_error(
						"{}: packed cache has no matching import document",
						path);
				if (checked.document->packedSourceStamp != checked.key.source.stamp)
					checked.stale = true;
				return checked;
			}

			// An absent source cannot be compared, so it stales nothing: the entry stays current
			// while its token holds, which is what keeps a project missing its sources loadable.
			if (compareSource)
			{
				const SourceStamp stamp = stampOf(store.GetFiles(), checked.key.source.key);
				if (stamp != SourceStamp() && stamp != checked.key.source.stamp)
					checked.stale = true;
			}

			return checked;
		}

		void
		requirePackedKey(
			const AssetStore& store,
			std::string_view  path,
			uint32_t          magic,
			uint64_t          token,
			std::string_view  what)
		{
			if (checkKey(store, path, magic, token, what).stale)
				core::throw_runtime_error(
					"{}: stale packed cache does not match its import document",
					path);
		}

		RegeneratedGroup
		regenerate(const AssetStore& store, CheckedKey&& checked, std::string_view what)
		{
			const SourceRef& source = checked.key.source;
			if (source.key.empty())
			{
				core::throw_runtime_error(
					"{}: written at another bake revision and no source was ever recorded, so it "
					"cannot be regenerated; re-import it",
					what);
			}
			if (!store.Exists(source.key))
			{
				core::throw_runtime_error(
					"{}: stale, and its source '{}' is not in the project to regenerate from",
					what,
					source.key);
			}

			// Regenerating at default parameters where the lost document said otherwise is
			// #413's exact shape, so a missing document refuses rather than guesses.
			if (!checked.document.has_value())
			{
				core::throw_runtime_error(
					"{}: stale, and the import document beside '{}' is gone, so the parameters it "
					"should regenerate at are unknowable; re-import the source",
					what,
					source.key);
			}

			return importGroup(store, source.key, std::move(*checked.document));
		}

	}

	RegeneratedGroup
	importGroup(const AssetStore& store, std::string_view sourceKey, ImportDocument&& document)
	{
		// The copied source lives only on the loose layer -- pack excludes it -- and the glTF
		// parser reads a file, so this is a read that must address the host. Textures are skipped:
		// a regeneration never re-extracts, so decoding them would spend an import's whole cost on
		// pixels nothing reads.
		RegeneratedGroup group{
			loadFromGltf(
				store.ResolveWritePath(sourceKey),
				{ .sampleRate = document.sampleRate, .textures = GltfTextures::kSkip }),
			SourceRef(),
			std::move(document),
		};
		group.ref.key            = std::string(sourceKey);
		group.ref.stamp          = stampOf(store.GetFiles(), sourceKey);
		group.ref.parametersHash = parametersHashOf(*group.document);
		return group;
	}

	bool
	AssetStore::GeometryIsStale(std::string_view path) const
	{
		const std::string extension = extensionOf(path);
		if (extension != c_MeshExtension && extension != c_SkeletonExtension &&
		    extension != c_AnimationExtension)
			core::throw_runtime_error(
				"'{}' is not a geometry cache entry, so it has no cache key to check",
				path);

		if (extension == c_MeshExtension)
			return checkKey(*this, path, magic::c_BMesh, AssetCodec<BMesh>::c_BakeToken, "bmesh")
			    .stale;
		if (extension == c_SkeletonExtension)
			return checkKey(*this, path, magic::c_BSkel, AssetCodec<Skeleton>::c_BakeToken, "bskel")
			    .stale;
		return checkKey(*this, path, magic::c_BAnim, AssetCodec<AnimationSet>::c_BakeToken, "banim")
		    .stale;
	}

	SourceRef
	AssetStore::GeometryGroupSource(std::string_view path) const
	{
		const std::string extension = extensionOf(path);
		if (extension != c_MeshExtension && extension != c_SkeletonExtension &&
		    extension != c_AnimationExtension)
			core::throw_runtime_error(
				"'{}' is not a geometry cache entry, so it has no cache key to check",
				path);

		uint32_t         magic = magic::c_BMesh;
		std::string_view what  = "bmesh";
		if (extension == c_MeshExtension)
		{
			magic = magic::c_BMesh;
			what  = "bmesh";
		}
		else if (extension == c_SkeletonExtension)
		{
			magic = magic::c_BSkel;
			what  = "bskel";
		}
		else if (extension == c_AnimationExtension)
		{
			magic = magic::c_BAnim;
			what  = "banim";
		}

		MountedFileReader reader(GetFiles(), path, what);
		SourceRef         current;
		current.key = cache::peekKey(reader, magic, what).source.key;
		if (current.key.empty())
			return current;
		if (const auto owner = FindImportForOutput(path))
		{
			current.key            = importedSourceKeyFor(owner->documentKey, owner->document);
			current.parametersHash = parametersHashOf(owner->document);
			current.stamp =
				IsReadOnly() ? owner->document.packedSourceStamp : stampOf(*m_Files, current.key);
			return current;
		}

		current.stamp = stampOf(*m_Files, current.key);

		const std::string documentKey = importDocumentKeyFor(current.key);
		if (GetFiles().Exists(documentKey))
			current.parametersHash = parametersHashOf(loadImportDocument(GetFiles(), documentKey));
		return current;
	}

	MeshRefs
	AssetStore::LoadRegenMeshRefs(std::string_view path) const
	{
		const CheckedKey checked =
			checkKey(*this, path, magic::c_BMesh, AssetCodec<BMesh>::c_BakeToken, "bmesh", false);
		if (checked.document)
		{
			MeshRefs refs;
			refs.skeleton = checked.document->skeleton;
			for (const MaterialBinding& binding : checked.document->bindings)
				refs.materials.push_back(binding.material);
			for (const MaterialOverrideBinding& entry : checked.document->materialOverrides)
				refs.materials.push_back(entry.material);
			return refs;
		}
		if (checked.stale)
			core::throw_runtime_error(
				"bmesh '{}': stale cache has no import document, so its references cannot be known",
				path);
		return {};
	}

	std::string
	AssetStore::LoadRegenAnimationSkeletonPath(std::string_view path) const
	{
		if (!IsReadOnly())
		{
			MountedFileReader      reader(GetFiles(), path, "banim");
			const cache::PeekedKey key = cache::peekKey(reader, magic::c_BAnim, "banim");
			if (key.bakeToken != AssetCodec<AnimationSet>::c_BakeToken)
			{
				const std::string documentKey = importDocumentKeyFor(key.source.key);
				if (!GetFiles().Exists(documentKey))
				{
					core::throw_runtime_error(
						"'{}': written at another bake revision and the import document beside "
						"'{}' "
						"is gone, so the rig it names cannot be known",
						path,
						key.source.key);
				}

				const std::string rig = loadImportDocument(GetFiles(), documentKey).skeleton;
				if (rig.empty())
				{
					core::throw_runtime_error(
						"'{}': written at another bake revision and its import document names no "
						"skeleton; run `assetlib_cli migrate` to record the one it already uses",
						path);
				}
				return rig;
			}
		}
		return loadAnimationSkeletonPath(*m_Files, path);
	}

	RegenMesh
	AssetStore::LoadRegenMesh(std::string_view path) const
	{
		ZoneScopedN("assetlib load bmesh");
		ZoneTextF("%.*s", static_cast<int>(path.size()), path.data());

		CheckedKey checked =
			checkKey(*this, path, magic::c_BMesh, AssetCodec<BMesh>::c_BakeToken, "bmesh");
		if (IsReadOnly() && checked.stale)
			core::throw_runtime_error(
				"{}: stale packed cache does not match its import document",
				path);
		if (!checked.stale)
		{
			RegenMesh current{ load<BMesh>(*m_Files, path), {} };
			current.sourceKey = checked.key.source.key;
			current.bindings  = bindingSnapshot(
				current.mesh,
				checked.document.value_or(ImportDocument{}),
				current.unboundBindings);
			return current;
		}

		RegeneratedGroup group = regenerate(*this, std::move(checked), "bmesh");
		if (group.import.meshes.empty())
		{
			core::throw_runtime_error(
				"'{}': its re-exported source no longer carries a mesh; restore it in the DCC, or "
				"delete this file",
				path);
		}

		RegenMesh current{ toBMesh(group.import), {} };
		current.sourceKey = group.ref.key;
		generateTangents(current.mesh);
		requireUniqueSubmeshNames(current.mesh);
		current.mesh.source = group.ref;
		if (isSkinned(current.mesh))
		{
			current.mesh.skeletonSignature = skeletonSignature(group.import.skeleton);
			current.mesh.skeletonBoneNames = skeletonBoneNames(group.import.skeleton);
			if (group.document->skeleton.empty())
			{
				core::throw_runtime_error(
					"'{}': its source carries a rig but the import document beside it names no "
					"skeleton; run `assetlib_cli migrate` to record the one it already uses",
					path);
			}
		}
		current.bindings = bindingSnapshot(current.mesh, *group.document, current.unboundBindings);
		return current;
	}

	Skeleton
	AssetStore::LoadRegenSkeleton(std::string_view path) const
	{
		ZoneScopedN("assetlib load bskel");
		ZoneTextF("%.*s", static_cast<int>(path.size()), path.data());

		if (IsReadOnly())
		{
			requirePackedKey(
				*this,
				path,
				magic::c_BSkel,
				AssetCodec<Skeleton>::c_BakeToken,
				"bskel");
			return load<Skeleton>(*m_Files, path);
		}

		CheckedKey checked =
			checkKey(*this, path, magic::c_BSkel, AssetCodec<Skeleton>::c_BakeToken, "bskel");
		if (!checked.stale)
			return load<Skeleton>(*m_Files, path);

		RegeneratedGroup group = regenerate(*this, std::move(checked), "bskel");
		if (group.import.skeleton.bones.empty())
		{
			core::throw_runtime_error(
				"'{}': its re-exported source no longer carries a rig; restore the skeleton in the "
				"DCC, or delete this file and its dependents",
				path);
		}

		Skeleton skeleton = group.import.skeleton;
		skeleton.source   = group.ref;
		return skeleton;
	}

	AnimationSet
	AssetStore::LoadRegenAnimations(std::string_view path) const
	{
		ZoneScopedN("assetlib load banim");
		ZoneTextF("%.*s", static_cast<int>(path.size()), path.data());

		if (IsReadOnly())
		{
			requirePackedKey(
				*this,
				path,
				magic::c_BAnim,
				AssetCodec<AnimationSet>::c_BakeToken,
				"banim");
			return load<AnimationSet>(*m_Files, path);
		}

		CheckedKey checked =
			checkKey(*this, path, magic::c_BAnim, AssetCodec<AnimationSet>::c_BakeToken, "banim");
		if (!checked.stale)
			return load<AnimationSet>(*m_Files, path);

		RegeneratedGroup group = regenerate(*this, std::move(checked), "banim");
		if (group.import.animations.clips.empty())
		{
			core::throw_runtime_error(
				"'{}': its re-exported source no longer carries clips; restore them in the DCC, or "
				"delete this file",
				path);
		}
		if (group.import.skeleton.bones.empty())
		{
			core::throw_runtime_error(
				"'{}': its re-exported source no longer carries a rig, so its clips address "
				"nothing",
				path);
		}

		AnimationSet clips = group.import.animations;
		clips.source       = group.ref;

		const std::string rigKey = group.document->skeleton;
		if (rigKey.empty())
		{
			core::throw_runtime_error(
				"'{}': its import document names no skeleton, so which rig its clips address "
				"cannot "
				"be known; run `assetlib_cli migrate` to record the one it already uses",
				path);
		}
		clips.skeleton = rigKey;

		const Skeleton skeleton = LoadRegenSkeleton(rigKey);

		// The group's own mesh may itself be stale on disk, where the walk below cannot read it;
		// measured from the regenerated form, its box is never the one missing. Tangents first:
		// the box's signature hashes the vertex layout, and every consumer holds the mesh with
		// them generated -- a box keyed to the raw import would never be found.
		BMesh mesh = toBMesh(group.import);
		generateTangents(mesh);

		// Ahead of every box: a box measured before the clips are grounded describes a rig standing
		// somewhere the runtime will never draw it.
		const std::span<const ClipFloor> authored = group.document->clipFloors;

		groundClipsForRig(GetFiles(), clips, std::span<const BMesh>(&mesh, 1), skeleton, authored);

		bakeBoundsForRig(*this, clips, normalizeRef(rigKey), skeleton, authored);
		bakePosedBounds(clips, mesh, skeleton);

		return clips;
	}

	Skeleton
	RigResolver::Resolve(const AssetStore& store, const std::string_view key)
	{
		{
			const std::lock_guard lock(m_Mutex);
			if (const auto it = m_Rigs.find(key); it != m_Rigs.end())
				return it->second;
		}

		// Resolved outside the lock: a stale rig re-cooks from its source, and holding the lock
		// across that parse would queue every other container behind it. Two threads racing one
		// rig both resolve it and agree -- a parse spent, and nothing else.
		Skeleton rig = store.LoadRegenSkeleton(key);

		const std::lock_guard lock(m_Mutex);
		return m_Rigs.try_emplace(std::string(key), std::move(rig)).first->second;
	}

	namespace
	{
		template <typename T, std::invocable<T&, const Skeleton&> Remap>
		void
		remapIfGrown(
			RigResolver&      rigs,
			const AssetStore& store,
			T&                container,
			std::string_view  skeletonKey,
			Remap&&           remap)
		{
			if (skeletonKey.empty())
				return;

			const Skeleton rig = rigs.Resolve(store, skeletonKey);
			if (container.skeletonSignature == skeletonSignature(rig))
				return;

			(void)remap(container, rig);
		}
	}

	void
	remapToItsRig(RigResolver& rigs, const AssetStore& store, AnimationSet& clips)
	{
		remapIfGrown(rigs, store, clips, clips.skeleton, remapAnimations);
	}

	void
	remapToItsRig(RigResolver& rigs, const AssetStore& store, RegenMesh& mesh)
	{
		remapIfGrown(rigs, store, mesh.mesh, mesh.bindings.skeleton, remapMesh);
	}
}
