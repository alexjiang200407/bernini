#include <assetlib/asset_import.h>

#include <assetlib/AssetStore.h>

#include <assetlib/import_document.h>

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
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <core/err/util.h>
#include <core/file/file.h>

#include <assetlib_structs/magic.h>

#include "CheckedFileReader.h"
#include "cache_io.h"
#include "import_bounds.h"
#include "ref_paths.h"

namespace assetlib
{
	void
	requireSelfContainedSource(const std::filesystem::path& source)
	{
		core::throw_runtime_error_if(
			extensionOf(source.generic_string()) != ".glb",
			"'{}': import needs a self-contained source; export as .glb",
			source.string());
	}

	void
	requireUniqueSubmeshNames(const BMesh& mesh)
	{
		auto seen = std::unordered_set<std::string_view>();
		for (const Submesh& submesh : mesh.submeshes)
		{
			const std::string_view name = mesh.stringPool.at(submesh.nameOffset);
			core::throw_runtime_error_if(
				!seen.insert(name).second,
				"'{}' names two submeshes; the name is what a material binding addresses, so name "
				"the meshes in the DCC",
				name);
		}
	}

	std::filesystem::path
	importedSourcePathFor(const std::filesystem::path& dataRoot, std::string_view name)
	{
		return dataRoot / c_MeshesSrcDirectoryName / std::format("{}.glb", name);
	}

	std::filesystem::path
	importDocumentPathFor(const std::filesystem::path& dataRoot, std::string_view name)
	{
		return dataRoot / c_MeshesSrcDirectoryName /
		       std::format("{}{}", name, c_ImportDocumentExtension);
	}

	namespace
	{
		/** The one construction the key and the document share -- a parameter added in only one
		    place cannot silently split the hash from the file. */
		ImportDocument
		parametersOnly(float sampleRate)
		{
			ImportDocument document;
			document.sampleRate = sampleRate;
			return document;
		}

		/** The bindings `mesh` carries, in first-appearance order -- what a document records. */
		std::vector<MaterialBinding>
		bindingsOf(const BMesh& mesh)
		{
			auto bindings = std::vector<MaterialBinding>();
			for (const Submesh& submesh : mesh.submeshes)
			{
				// >= size subsumes the c_InvalidIndex sentinel (0xFFFFFFFF).
				if (submesh.material >= mesh.materials.size())
					continue;
				bindings.emplace_back(
					std::string(mesh.stringPool.at(submesh.nameOffset)),
					mesh.materials[submesh.material]);
			}
			return bindings;
		}
	}

	SourceRef
	copyImportedSource(const std::filesystem::path& source, const ImportTarget& target)
	{
		requireSelfContainedSource(source);

		const std::filesystem::path copied = importedSourcePathFor(target.dataRoot, target.name);
		std::filesystem::create_directories(copied.parent_path());

		std::error_code ec;
		std::filesystem::copy_file(
			source,
			copied,
			std::filesystem::copy_options::overwrite_existing,
			ec);
		core::throw_runtime_error_if(
			static_cast<bool>(ec),
			"cannot copy '{}' to '{}': {}",
			source.string(),
			copied.string(),
			ec.message());

		SourceRef ref;
		ref.key =
			std::format("{}/{}", c_MeshesSrcDirectoryName, copied.filename().generic_string());
		ref.stamp.size                     = std::filesystem::file_size(copied);
		const std::optional<uint64_t> hash = core::file::hash_file(copied);
		core::throw_runtime_error_if(
			!hash.has_value(),
			"cannot hash '{}' after copying it",
			copied.string());
		ref.stamp.hash     = *hash;
		ref.parametersHash = parametersHashOf(parametersOnly(target.sampleRate));
		return ref;
	}

	void
	writeImportedDocument(const ImportTarget& target, const BMesh* mesh)
	{
		ImportDocument document = parametersOnly(target.sampleRate);
		if (mesh != nullptr)
			document.bindings = bindingsOf(*mesh);

		core::file::write_atomic(
			importDocumentPathFor(target.dataRoot, target.name),
			serializeImportDocument(document));
	}

	std::vector<std::string>
	applyBindings(BMesh& mesh, std::span<const MaterialBinding> bindings)
	{
		auto bySubmesh = std::unordered_map<std::string_view, std::string_view>();
		for (const MaterialBinding& binding : bindings)
			bySubmesh.emplace(binding.submesh, binding.material);

		mesh.materials.clear();
		auto indexOf = std::unordered_map<std::string_view, uint32_t>();
		auto matched = std::unordered_set<std::string_view>();
		for (Submesh& submesh : mesh.submeshes)
		{
			const auto found = bySubmesh.find(mesh.stringPool.at(submesh.nameOffset));
			if (found == bySubmesh.end())
			{
				submesh.material = c_InvalidIndex;
				continue;
			}
			const auto [slot, added] =
				indexOf.emplace(found->second, static_cast<uint32_t>(mesh.materials.size()));
			if (added)
				mesh.materials.emplace_back(found->second);
			submesh.material = slot->second;
			matched.insert(found->first);
		}

		auto unbound = std::vector<std::string>();
		for (const MaterialBinding& binding : bindings)
			if (!matched.contains(binding.submesh))
				unbound.emplace_back(binding.submesh);
		return unbound;
	}

	void
	rebindSubmeshInDocument(
		const std::filesystem::path& dataRoot,
		std::string_view             sourceKey,
		std::string_view             submesh,
		std::string_view             material)
	{
		core::throw_runtime_error_if(
			sourceKey.empty(),
			"'{}': no source was ever recorded, so there is no import document to rebind in",
			submesh);

		const std::filesystem::path documentPath = dataRoot / importDocumentKeyFor(sourceKey);
		core::throw_runtime_error_if(
			!std::filesystem::exists(documentPath),
			"'{}': no import document to rebind in -- re-import the source",
			documentPath.string());
		ImportDocument document = loadImportDocument(documentPath);

		const auto found = std::ranges::find(document.bindings, submesh, &MaterialBinding::submesh);
		if (found != document.bindings.end())
			found->material = std::string(material);
		else
			document.bindings.emplace_back(std::string(submesh), std::string(material));

		core::file::write_atomic(documentPath, serializeImportDocument(document));
	}

	std::vector<ReauthoredDocument>
	reauthorImportDocuments(const std::filesystem::path& dataRoot)
	{
		namespace fs = std::filesystem;

		core::throw_runtime_error_if(
			!fs::is_directory(dataRoot),
			"'{}' is not a directory",
			dataRoot.string());

		// Which mesh claims which source, by the frozen header alone -- readable whatever the
		// file's bake revision, which is what lets a stale mesh still name its document.
		auto claims     = std::unordered_map<std::string, std::vector<fs::path>>();
		auto unreadable = std::vector<std::string>();

		std::error_code ec;
		const auto      walk = fs::directory_options::skip_permission_denied;
		for (const fs::directory_entry& entry :
		     fs::recursive_directory_iterator(dataRoot, walk, ec))
		{
			if (!entry.is_regular_file(ec) || entry.path().extension() != c_MeshExtension)
				continue;
			try
			{
				CheckedFileReader      reader(entry.path(), "bmesh");
				const cache::PeekedKey key = cache::peekKey(reader, magic::c_BMesh, "bmesh");
				if (!key.source.key.empty())
					claims[key.source.key].push_back(entry.path());
			}
			catch (const std::exception&)
			{
				// Which source it claims is unknowable, so every claimless document below has to
				// treat this mesh as possibly its own.
				unreadable.push_back(mountKeyFor(dataRoot, entry.path()));
			}
		}

		auto report = std::vector<ReauthoredDocument>();
		for (const fs::directory_entry& entry :
		     fs::recursive_directory_iterator(dataRoot, walk, ec))
		{
			if (!entry.is_regular_file(ec) || entry.path().extension() != c_ImportDocumentExtension)
				continue;

			const std::string key       = mountKeyFor(dataRoot, entry.path());
			const std::string sourceKey = importedSourceKeyFor(key);

			ReauthoredDocument result{ key, ReauthoredDocument::Outcome::kUnchanged, {} };
			try
			{
				const auto   claimed   = claims.find(sourceKey);
				const size_t claimants = claimed == claims.end() ? 0 : claimed->second.size();
				core::throw_runtime_error_if(
					claimants > 1,
					"{} meshes derive from '{}', so which one's bindings this document should "
					"record is ambiguous",
					claimants,
					sourceKey);

				// A claimless document is a clips-only group, which binds nothing -- unless a
				// mesh header would not read, in which case that mesh may be the claimant and
				// clearing the bindings would silently destroy them.
				core::throw_runtime_error_if(
					claimants == 0 && !unreadable.empty(),
					"no mesh claims '{}', but '{}' has an unreadable header and may be its "
					"claimant; fix that mesh and re-run",
					sourceKey,
					unreadable.front());

				const std::vector<std::byte> bytes =
					core::file::read_file_bytes(entry.path().string());
				ImportDocument document = deserializeImportDocument(
					std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

				document.bindings =
					claimants == 0 ?
						std::vector<MaterialBinding>() :
						bindingsOf(
							AssetCodec<BMesh>::Deserialize(
								core::file::read_file_bytes(claimed->second.front().string())));

				const std::string serialized = serializeImportDocument(document);
				if (std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()) !=
				    serialized)
				{
					core::file::write_atomic(entry.path(), serialized);
					result.outcome = ReauthoredDocument::Outcome::kRewritten;
				}
			}
			catch (const std::exception& e)
			{
				result.outcome = ReauthoredDocument::Outcome::kFailed;
				result.message = e.what();
			}
			report.push_back(std::move(result));
		}

		for (const auto& [sourceKey, meshes] : claims)
		{
			const std::string documentKey = importDocumentKeyFor(sourceKey);
			if (fs::exists(dataRoot / documentKey))
				continue;
			report.push_back(
				{ documentKey,
			      ReauthoredDocument::Outcome::kFailed,
			      std::format(
					  "'{}' records '{}' as its source but no document stands beside it; "
					  "re-import the source",
					  mountKeyFor(dataRoot, meshes.front()),
					  sourceKey) });
		}

		return report;
	}

	void
	writeImportedMesh(const AssetStore& store, const BMesh& mesh, std::string_view bmeshKey)
	{
		// An import aimed at a subfolder lands at `Meshes/<folder>/`, which nothing else creates.
		std::filesystem::create_directories(store.ResolveWritePath(bmeshKey).parent_path());
		store.Save(mesh, bmeshKey);
	}

	void
	writeImportedRig(
		const AssetStore&       store,
		const imp::BMeshImport& imported,
		BMesh&                  mesh,
		std::string_view        bskelKey,
		std::string_view        banimKey,
		bool                    writeClips,
		const SourceRef&        source)
	{
		if (imported.skeleton.bones.empty())
			return;

		// A project scaffolded before these categories existed has neither directory.
		std::filesystem::create_directories(store.ResolveWritePath(bskelKey).parent_path());

		Skeleton skeleton = imported.skeleton;
		skeleton.source   = source;
		store.Save(skeleton, bskelKey);
		mesh.skeleton = std::string(bskelKey);

		if (!writeClips || imported.animations.clips.empty())
			return;

		// The clip set names the rig by the same path the mesh does, so all three agree on which file
		// the joint indices are addressed against.
		std::filesystem::create_directories(store.ResolveWritePath(banimKey).parent_path());

		AnimationSet clips = imported.animations;
		clips.skeleton     = mesh.skeleton;
		clips.source       = source;
		bakePosedBounds(clips, mesh, imported.skeleton);
		store.Save(clips, banimKey);
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
				if (skeletonSignature(
						AssetCodec<Skeleton>::Deserialize(
							core::file::read_file_bytes(entry.path().string()))) == wanted)
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

				bakePosedBounds(
					clips,
					AssetCodec<BMesh>::Deserialize(
						core::file::read_file_bytes(entry.path().string())),
					skeleton);
			}
			catch (const std::exception&)
			{
				// A mesh that cannot be read has no box to bake; the asset scan is where a
				// broken one gets reported.
			}
		}
	}

	void
	writeImportedClips(
		const AssetStore&       store,
		const imp::BMeshImport& imported,
		std::string_view        banimKey,
		const SourceRef&        source)
	{
		if (imported.animations.clips.empty())
			throw std::runtime_error("this file carries no animation to import");

		// The clips are per-bone samples addressed by index, so without the rig they were authored
		// against there is nothing to say which bone each one drives.
		if (imported.skeleton.bones.empty())
			throw std::runtime_error("this file carries no rig, so its clips address nothing");

		const std::filesystem::path rig =
			findMatchingSkeleton(store.GetDataRoot(), imported.skeleton);
		if (rig.empty())
		{
			throw std::runtime_error(
				"no skeleton in this project matches this file's rig. Import one of these "
				"files with the mesh turned on first, which writes the rig these clips "
				"attach to.");
		}

		std::filesystem::create_directories(store.ResolveWritePath(banimKey).parent_path());

		AnimationSet clips = imported.animations;
		clips.skeleton     = store.KeyFor(rig);
		clips.source       = source;

		// Measured against the rig the clips will resolve at load, not the imported copy: the two
		// share a signature but a re-authored bind pose deliberately does not change one.
		bakeBoundsForRig(
			clips,
			store.GetDataRoot(),
			normalizePath(clips.skeleton),
			store.Load<Skeleton>(clips.skeleton));

		store.Save(clips, banimKey);
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
			// An empty path means the import wrote no such folder; a path that *is* the category root
			// would mean the import's subdirectory got lost somewhere, and taking the root down with it
			// is not a recovery.
			if (dir.existed || dir.path.empty() ||
			    dir.path.lexically_normal() == dir.categoryRoot.lexically_normal())
				continue;

			fs::remove_all(dir.path, ec);
		}
	}
}
