#include <assetlib/AssetStore.h>
#include <assetlib/pak_pack.h>

#include <assetlib/RegenMesh.h>
#include <assetlib/asset_refs.h>
#include <assetlib/banim_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/container_format.h>
#include <assetlib/env_bake.h>
#include <assetlib/pak_io.h>
#include <assetlib/project_layout.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BVat.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>
#include <core/hash.h>

#include "ref_paths.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		bool
		isAuthoringSource(const std::filesystem::path& relative)
		{
			for (const std::filesystem::path& part : relative)
			{
				if (part == c_TexturesSrcDirectoryName || part == c_MeshesSrcDirectoryName)
					return true;
			}
			return false;
		}

		std::string
		relativeKey(const std::filesystem::path& file, const std::filesystem::path& dataRoot)
		{
			return normalizeRef(file.lexically_relative(dataRoot).generic_string());
		}

		// Sorted, because directory-iteration order is not the same on two filesystems and the walk
		// order is the order payloads land in. Without this an archive is only reproducible per
		// machine, which is no use to anyone diffing or caching a shipped one.
		std::vector<std::filesystem::path>
		filesUnder(const std::filesystem::path& dataRoot)
		{
			std::vector<std::filesystem::path> out;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
			{
				if (entry.is_regular_file())
					out.push_back(entry.path());
			}
			std::ranges::sort(out);
			return out;
		}

		// A bake behind its routed source has nowhere to catch up once shipped, the same bind as a
		// stale `.bvat`. A route that never recorded a source has nothing to be behind (the
		// committed sets predate recording); one whose source is gone fails the bake loudly, which
		// is the archive's job. Runs over a snapshot taken before the pack walk's own: a bake
		// writes content-addressed maps, so a re-bake adds files that walk must still see.
		uint32_t
		rebakeStaleEnvs(const AssetStore& store, const std::vector<std::filesystem::path>& files)
		{
			const core::file::LooseFileSystem loose(store.GetDataRoot());

			uint32_t rebaked = 0;
			for (const std::filesystem::path& file : files)
			{
				const auto type = assetTypeFromExtension(file);
				if (type != AssetType::kSky && type != AssetType::kEnvLighting)
					continue;

				try
				{
					if (type == AssetType::kSky)
					{
						BSky sky = loadSky(file);
						if (!isSkyBakeStale(sky, loose))
							continue;
						store.BakeSky(sky);
						saveSky(sky, file);
					}
					else
					{
						BEnvLighting lighting = loadEnvLighting(file);
						if (!isEnvLightingBakeStale(lighting, loose))
							continue;
						store.BakeEnvLighting(lighting);
						saveEnvLighting(lighting, file);
					}
				}
				catch (const std::exception& error)
				{
					core::throw_runtime_error(
						"assetlib::packProject: '{}': {}",
						relativeKey(file, store.GetDataRoot()),
						error.what());
				}
				++rebaked;
			}
			return rebaked;
		}

		// A packed .bvat's inputs may ship beside it with nowhere to write a re-bake, so the archive
		// carries one that is correct at pack time rather than one the runtime has to judge. The
		// group axis is part of the question: geometry regenerated in memory leaves the disk
		// stamps untouched, so a bake over a group that is a cache miss is stale however well its
		// own stamps hold -- shipping it would split the VAT tier from the skinned one.
		uint32_t
		rebakeStaleVats(const AssetStore& store, const std::vector<std::filesystem::path>& files)
		{
			const core::file::LooseFileSystem loose(store.GetDataRoot());

			uint32_t rebaked = 0;
			for (const std::filesystem::path& file : files)
			{
				if (assetTypeFromExtension(file) != AssetType::kVat)
					continue;

				// Read whole, not tables alone: a bake from before a format change reads its
				// tables cleanly and is stale all the same, and only the full read asks for the
				// chunks the runtime will. The archive copies every byte of it next, so the pixels
				// cost nothing here that they do not cost anyway.
				bool fresh = false;
				try
				{
					const BVat vat = loadVat(file);
					fresh          = !vatIsStale(vat, loose) && !store.GeometryIsStale(vat.mesh) &&
					                 !store.GeometryIsStale(vat.skeleton) &&
					                 !store.GeometryIsStale(vat.animations);
				}
				catch (const std::exception&)
				{}
				if (fresh)
					continue;

				const VatRefs refs = loadVatRefs(file);
				saveVat(bakeVat(store, VatBakeDesc{ refs.mesh, refs.animations }), file);
				++rebaked;
			}
			return rebaked;
		}

		/**
		 * The bytes the archive will carry for a geometry entry: the seam's answer, serialized.
		 * One construction, shared by the pack loop and the `.bvat` re-stamp, so the stamps a
		 * vat records can never drift from the entry the archive actually stores.
		 */
		std::vector<std::byte>
		currentGeometryBytes(const AssetStore& store, AssetType type, std::string_view key)
		{
			switch (type)
			{
			case AssetType::kMesh:
			{
				RegenMesh current = store.LoadRegenMesh(key);
				core::throw_runtime_error_if(
					!current.unboundBindings.empty(),
					"assetlib::packProject: '{}' binds submesh '{}', which the mesh does not "
					"have; rebind or re-export",
					key,
					current.unboundBindings.front());
				return serialize(current.mesh);
			}
			case AssetType::kSkeleton:
				return serializeSkeleton(store.LoadRegenSkeleton(key));
			case AssetType::kAnimation:
				return serializeAnimations(store.LoadRegenAnimations(key));
			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kVat:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kEnvironment:
			case AssetType::kImportDocument:
				break;
			}
			core::throw_runtime_error("assetlib::packProject: '{}' is not geometry", key);
		}

		/** Each geometry key's archived bytes, computed once per pack however often asked. */
		class ArchivedGeometry
		{
		public:
			explicit ArchivedGeometry(const AssetStore& store) : m_Store(store) {}

			ArchivedGeometry(const ArchivedGeometry&) = delete;
			ArchivedGeometry(ArchivedGeometry&&)      = delete;
			ArchivedGeometry&
			operator=(const ArchivedGeometry&) = delete;
			ArchivedGeometry&
			operator=(ArchivedGeometry&&) = delete;

			[[nodiscard]] const std::vector<std::byte>&
			BytesFor(AssetType type, const std::string& key)
			{
				auto found = m_Bytes.find(key);
				if (found == m_Bytes.end())
					found = m_Bytes.emplace(key, currentGeometryBytes(m_Store, type, key)).first;
				return found->second;
			}

			/** What the geometry at `key` stamps as inside the archive -- the stored bytes,
			    hashed. The type is the key's own extension; a caller cannot mispair them. */
			[[nodiscard]] SourceStamp
			StampFor(const std::string& key)
			{
				const std::string extension = extensionOf(key);
				const AssetType   type = extension == c_MeshExtension     ? AssetType::kMesh :
				                         extension == c_SkeletonExtension ? AssetType::kSkeleton :
				                                                            AssetType::kAnimation;

				const std::vector<std::byte>& bytes = BytesFor(type, key);
				return SourceStamp{
					bytes.size(),
					core::hash_bytes(bytes.data(), bytes.size(), core::hash_seed())
				};
			}

		private:
			const AssetStore&                                       m_Store;
			std::unordered_map<std::string, std::vector<std::byte>> m_Bytes;
		};
	}

	PackReport
	packProject(const AssetStore& store, const PackDesc& desc)
	{
		// The walk and the rebake address the writable layer: packing reads what is on disk under
		// the data root, not what a wider mount would also answer for.
		const std::filesystem::path& dataRoot = store.GetDataRoot();

		if (!std::filesystem::is_directory(dataRoot))
			core::throw_runtime_error(
				"assetlib::packProject: '{}' is not a directory",
				dataRoot.string());

		PackReport report;
		report.envsRebaked = rebakeStaleEnvs(store, filesUnder(dataRoot));

		const std::vector<std::filesystem::path> files = filesUnder(dataRoot);
		report.vatsRebaked                             = rebakeStaleVats(store, files);

		const core::file::LooseFileSystem loose(dataRoot);

		// The seam's answer per geometry key, computed once however many askers -- the entry's
		// own pack, and any `.bvat` stamping against it.
		ArchivedGeometry archived(store);

		PakWriter writer(desc.target);
		for (const std::filesystem::path& file : files)
		{
			const std::filesystem::path relative = file.lexically_relative(dataRoot);
			if (isAuthoringSource(relative))
				continue;

			const std::optional<AssetType> type = assetTypeFromExtension(file);
			if (!type.has_value())
			{
				++report.skippedByExtension[file.extension().generic_string()];
				continue;
			}
			// Authored, and the game never reads it: a read-only store uses the baked-in bindings.
			if (type == AssetType::kImportDocument)
				continue;

			const std::string                          key   = relativeKey(file, dataRoot);
			const std::optional<core::file::FileStamp> stamp = loose.Stat(key);
			if (!stamp.has_value())
				core::throw_runtime_error("assetlib::packProject: cannot stat '{}'", key);

			// Read once, whatever the entry becomes: the verbatim payload for most of the bytes
			// (textures above all), and the rebaked-or-not comparison for geometry.
			auto diskBytes = loose.Read(key);

			// Empty means "pack the disk bytes verbatim".
			auto regenerated = std::optional<std::vector<std::byte>>();

			switch (*type)
			{
			// Geometry carries what the seam answers; a group it cannot serve fails the pack.
			case AssetType::kMesh:
			case AssetType::kSkeleton:
			case AssetType::kAnimation:
				regenerated = archived.BytesFor(*type, key);
				break;

			case AssetType::kVat:
			{
				// The bake stamped the files on disk, but the archive stores the seam's answers
				// for those same keys -- re-stamped here, so a staleness question asked *inside*
				// the archive still answers fresh. The pixels were made current by
				// rebakeStaleVats above. A vat whose stamps already describe the archived bytes
				// -- the ordinary pack, nothing regenerated and nothing rebound -- copies
				// verbatim rather than paying a decode and a re-encode.
				BVat vat = loadVat(file);

				const SourceStamp meshStamp       = archived.StampFor(normalizeRef(vat.mesh));
				const SourceStamp skeletonStamp   = archived.StampFor(normalizeRef(vat.skeleton));
				const SourceStamp animationsStamp = archived.StampFor(normalizeRef(vat.animations));

				if (meshStamp != vat.meshStamp || skeletonStamp != vat.skeletonStamp ||
				    animationsStamp != vat.animationsStamp)
				{
					vat.meshStamp       = meshStamp;
					vat.skeletonStamp   = skeletonStamp;
					vat.animationsStamp = animationsStamp;
					regenerated         = serializeVat(vat);
				}
				break;
			}

			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kEnvironment:
			case AssetType::kImportDocument:
				break;
			}

			const bool isGeometry = *type == AssetType::kMesh || *type == AssetType::kSkeleton ||
			                        *type == AssetType::kAnimation;
			if (isGeometry && regenerated.has_value() && *regenerated != diskBytes)
				++report.geometryRebaked;

			const std::vector<std::byte> bytes =
				regenerated.has_value() ? std::move(*regenerated) : std::move(diskBytes);

			// Asked while the bytes are already in hand, which is the only moment packing reads a
			// material at all.
			if (type == AssetType::kMaterial && drawsLoose(deserializeMaterial(bytes), loose))
				report.materialsDrawingLoose.push_back(key);

			// The stamp describes the stored bytes -- a size the payload does not match would
			// read as staleness inside the archive.
			writer.Add(key, bytes, core::file::FileStamp{ bytes.size(), stamp->mtime });

			++report.entries;
			report.payloadBytes += bytes.size();
		}

		std::ranges::sort(report.materialsDrawingLoose);

		writer.Finish();
		return report;
	}
}
