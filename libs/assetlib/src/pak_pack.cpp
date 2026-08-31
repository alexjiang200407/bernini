#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>
#include <assetlib/envmap.h>
#include <assetlib/pak.h>

#include <assetlib/RegenMesh.h>
#include <assetlib/asset_refs.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/Skeleton.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>
#include <core/hash.h>

#include "ref_paths.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		// Beneath, not a component match: `Meshes` names a directory in each half, and only the
		// authored one is a source.
		bool
		isAuthoringSource(const std::filesystem::path& relative)
		{
			const std::string key = relative.generic_string();

			return isUnder(key, c_MeshSourcesDirectoryName) ||
			       isUnder(key, c_SourceTexturesDirectoryName);
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

		// A bake behind its routed source has nowhere to catch up once shipped. A route that never
		// recorded a source has nothing to be behind (the
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
						BSky sky = store.Load<BSky>(store.KeyFor(file));
						if (!isSkyBakeStale(sky, loose))
							continue;
						store.BakeSky(sky);
						store.Save(sky, store.KeyFor(file));
					}
					else
					{
						BEnvLighting lighting = store.Load<BEnvLighting>(store.KeyFor(file));
						if (!isEnvLightingBakeStale(lighting, loose))
							continue;
						store.BakeEnvLighting(lighting);
						store.Save(lighting, store.KeyFor(file));
					}
				}
				catch (const std::exception& error)
				{
					core::throw_runtime_error(
						"AssetStore::Pack: '{}': {}",
						relativeKey(file, store.GetDataRoot()),
						error.what());
				}
				++rebaked;
			}
			return rebaked;
		}

		/**
		 * The bytes the archive will carry for a geometry entry: the seam's answer, serialized.
		 * One construction, shared by every asker, so the entry the archive stores cannot drift
		 * from what a staleness question inside it later reads.
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
					"AssetStore::Pack: '{}' binds submesh '{}', which the mesh does not "
					"have; rebind or re-export",
					key,
					current.unboundBindings.front());
				return AssetCodec<BMesh>::Serialize(current.mesh);
			}
			case AssetType::kSkeleton:
				return AssetCodec<Skeleton>::Serialize(store.LoadRegenSkeleton(key));
			case AssetType::kAnimation:
				return AssetCodec<AnimationSet>::Serialize(store.LoadRegenAnimations(key));
			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kEnvironment:
			case AssetType::kImportDocument:
			case AssetType::kCount:
				break;
			}
			core::throw_runtime_error("AssetStore::Pack: '{}' is not geometry", key);
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
	AssetStore::Pack(const PackDesc& desc) const
	{
		// The walk and the rebake address the writable layer: packing reads what is on disk under
		// the data root, not what a wider mount would also answer for.
		const std::filesystem::path& dataRoot = GetDataRoot();

		if (!std::filesystem::is_directory(dataRoot))
			core::throw_runtime_error(
				"AssetStore::Pack: '{}' is not a directory",
				dataRoot.string());

		PackReport report;
		report.envsRebaked = rebakeStaleEnvs(*this, filesUnder(dataRoot));

		const std::vector<std::filesystem::path> files = filesUnder(dataRoot);

		const core::file::LooseFileSystem loose(dataRoot);

		// The seam's answer per geometry key, computed once however many askers.
		ArchivedGeometry archived(*this);

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
				core::throw_runtime_error("AssetStore::Pack: cannot stat '{}'", key);

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

			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kEnvironment:
			case AssetType::kImportDocument:
			case AssetType::kCount:
				break;
			}

			if (isGeometryContainer(*type) && regenerated.has_value() && *regenerated != diskBytes)
				++report.geometryRebaked;

			const std::vector<std::byte> bytes =
				regenerated.has_value() ? std::move(*regenerated) : std::move(diskBytes);

			// Asked while the bytes are already in hand, which is the only moment packing reads a
			// material at all.
			if (type == AssetType::kMaterial &&
			    drawsLoose(AssetCodec<BMaterial>::Deserialize(bytes), loose))
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
