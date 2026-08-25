#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/import_document.h>
#include <core/err/util.h>

#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <core/file/file.h>

#include "fs_util.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		/** `stored`, re-pointed if it names what the plan moves or anything beneath it. */
		std::string
		mapTarget(const RenamePlan& plan, const std::string& stored)
		{
			if (stored.empty())
				return stored;

			const std::string key = normalizeRef(stored);
			if (key == plan.from)
				return plan.to;
			if (isUnder(key, plan.from))
				return plan.to + key.substr(plan.from.size());

			return stored;
		}

		/** One referrer file: where it is now, and the bytes that can undo its rewrite. */
		struct PendingReferrer
		{
			std::filesystem::path  path;
			AssetType              type = AssetType::kMesh;
			std::vector<std::byte> original;
		};

		std::vector<std::byte>
		rewriteReferrer(const RenamePlan& plan, AssetType type, std::span<const std::byte> bytes)
		{
			switch (type)
			{
			case AssetType::kMesh:
			{
				BMesh mesh = AssetCodec<BMesh>::Deserialize(bytes);
				for (std::string& material : mesh.materials) material = mapTarget(plan, material);
				mesh.skeleton = mapTarget(plan, mesh.skeleton);
				return AssetCodec<BMesh>::Serialize(mesh);
			}

			case AssetType::kAnimation:
			{
				AnimationSet clips = AssetCodec<AnimationSet>::Deserialize(bytes);
				clips.skeleton     = mapTarget(plan, clips.skeleton);
				return AssetCodec<AnimationSet>::Serialize(clips);
			}

			case AssetType::kVat:
			{
				// The stamps are left alone here and recomputed by renameAsset once every input is
				// in its final place -- a rename rewrites the references inside a `.bmesh` and a
				// `.banim`, so their contents, and their stamps, do move. renameAsset then puts the
				// file under the name vatPathFor derives from these rewritten inputs, or the runtime
				// would never look for it.
				BVat vat       = AssetCodec<BVat>::Deserialize(bytes);
				vat.mesh       = mapTarget(plan, vat.mesh);
				vat.skeleton   = mapTarget(plan, vat.skeleton);
				vat.animations = mapTarget(plan, vat.animations);
				return AssetCodec<BVat>::Serialize(vat);
			}

			case AssetType::kMaterial:
			{
				BMaterial material            = AssetCodec<BMaterial>::Deserialize(bytes);
				material.pbr.baseColorTexture = mapTarget(plan, material.pbr.baseColorTexture);
				material.pbr.normalTexture    = mapTarget(plan, material.pbr.normalTexture);
				material.pbr.ormTexture       = mapTarget(plan, material.pbr.ormTexture);
				for (ChannelRoute& route : material.pbr.routes)
					route.texture = mapTarget(plan, route.texture);
				return AssetCodec<BMaterial>::Serialize(material);
			}

			case AssetType::kSky:
			{
				BSky sky       = AssetCodec<BSky>::Deserialize(bytes);
				sky.sky.source = mapTarget(plan, sky.sky.source);
				sky.sky.baked  = mapTarget(plan, sky.sky.baked);
				return AssetCodec<BSky>::Serialize(sky);
			}

			case AssetType::kEnvLighting:
			{
				BEnvLighting lighting = AssetCodec<BEnvLighting>::Deserialize(bytes);
				for (EnvMapRoute* route : { &lighting.prefilter, &lighting.irradiance })
				{
					route->source = mapTarget(plan, route->source);
					route->baked  = mapTarget(plan, route->baked);
				}
				return AssetCodec<BEnvLighting>::Serialize(lighting);
			}

			case AssetType::kEnvironment:
			{
				BEnv env     = AssetCodec<BEnv>::Deserialize(bytes);
				env.sky      = mapTarget(plan, env.sky);
				env.lighting = mapTarget(plan, env.lighting);
				return AssetCodec<BEnv>::Serialize(env);
			}

			case AssetType::kImportDocument:
			{
				ImportDocument document = AssetCodec<ImportDocument>::Deserialize(bytes);
				for (MaterialBinding& binding : document.bindings)
					binding.material = mapTarget(plan, binding.material);
				return AssetCodec<ImportDocument>::Serialize(document);
			}

			case AssetType::kTexture:
			case AssetType::kSkeleton:
			case AssetType::kCount:
				break;
			}

			throw std::runtime_error(
				"assetlib::renameAsset: a referrer of no kind that stores references");
		}

	}

	RenamePlan
	planRename(const AssetRefGraph& graph, std::string_view from, std::string_view to)
	{
		auto plan = RenamePlan();
		plan.from = normalizeRef(from);
		plan.to   = normalizeRef(to);

		requireInsideDataRoot("assetlib::planRename", plan.from);
		requireInsideDataRoot("assetlib::planRename", plan.to);

		if (plan.from == plan.to)
			throw std::runtime_error(
				"assetlib::planRename: '" + plan.from + "' is already named that");

		if (isUnder(plan.to, plan.from))
			throw std::runtime_error(
				"assetlib::planRename: cannot move '" + plan.from + "' inside itself");

		const std::filesystem::path fromPath = graph.DataRoot() / plan.from;
		const std::filesystem::path toPath   = graph.DataRoot() / plan.to;

		if (!std::filesystem::exists(fromPath))
			throw std::runtime_error("assetlib::planRename: '" + plan.from + "' does not exist");

		if (std::filesystem::is_directory(fromPath))
		{
			for (const AssetRef& edge : graph.Edges())
				if (isUnder(edge.target, plan.from))
					plan.referrers.push_back(edge);
		}
		else
		{
			plan.assetType = assetTypeFromExtension(plan.from);
			if (!plan.assetType)
				throw std::runtime_error(
					"assetlib::planRename: '" + plan.from +
					"' is not an asset this project stores anything about");

			if (assetTypeFromExtension(plan.to) != plan.assetType)
				throw std::runtime_error(
					"assetlib::planRename: renaming '" + plan.from + "' to '" + plan.to +
					"' would change what kind of asset it is");

			// Its source key is derived from its own path, so a lone rename would orphan the
			// source; renaming the directory moves the pair and stays allowed.
			core::throw_runtime_error_if(
				plan.assetType == AssetType::kImportDocument,
				"assetlib::planRename: '{}' sits beside the source it describes and cannot be "
				"renamed alone",
				plan.from);

			const std::span<const AssetRef> referrers = graph.ReferrersOf(plan.from);
			plan.referrers.assign(referrers.begin(), referrers.end());
		}

		// equivalent() is what tells a real collision from a case-only rename on a case-insensitive
		// filesystem, where the destination "exists" because it is the file being renamed.
		std::error_code ec;
		if (std::filesystem::exists(toPath) && !std::filesystem::equivalent(fromPath, toPath, ec))
			throw std::runtime_error("assetlib::planRename: '" + plan.to + "' already exists");

		if (!std::filesystem::is_directory(toPath.parent_path()))
			throw std::runtime_error(
				"assetlib::planRename: the directory to rename '" + plan.from +
				"' into does not exist");

		return plan;
	}

	RenameResult
	renameAsset(const RenamePlan& plan, const AssetStore& store)
	{
		const std::filesystem::path fromPath = store.GetDataRoot() / plan.from;
		const std::filesystem::path toPath   = store.GetDataRoot() / plan.to;

		// Unlike a deletion, a rename cannot shrug at a file that has vanished since the plan: there is
		// nothing to move, and rewriting the referrers anyway would break every one of them.
		if (!std::filesystem::exists(fromPath))
			return { RenameStatus::kFailed, "'" + plan.from + "' no longer exists" };

		std::error_code ec;
		if (std::filesystem::exists(toPath) && !std::filesystem::equivalent(fromPath, toPath, ec))
			return { RenameStatus::kFailed, "'" + plan.to + "' already exists" };

		// Read and rewrite every referrer before writing anything: a file that will not parse fails the
		// rename while the project is still untouched.
		auto referrers = std::vector<std::string>();
		for (const AssetRef& edge : plan.referrers) referrers.push_back(edge.referrer);
		std::ranges::sort(referrers);
		const auto duplicates = std::ranges::unique(referrers);
		referrers.erase(duplicates.begin(), duplicates.end());

		auto files = std::vector<PendingReferrer>();
		for (const std::string& referrer : referrers)
		{
			const std::optional<AssetType> type = assetTypeFromExtension(referrer);
			if (!type || *type == AssetType::kTexture)
				throw std::runtime_error(
					"assetlib::renameAsset: '" + referrer +
					"' is not a container that stores references");

			auto file = PendingReferrer();
			file.path = store.GetDataRoot() / referrer;
			file.type = *type;

			// Ordinary weather, not a caller error: the file may be locked, gone since the scan, or --
			// the scan reads a mesh's material chunk alone -- malformed past the part the scan saw.
			// The rewrite is derived here only to prove it can be, and again at write time: referrers
			// include whole meshes, and holding two copies of every one would double the peak memory.
			try
			{
				file.original = core::file::read_file_bytes(file.path.string());
				(void)rewriteReferrer(plan, *type, file.original);
			}
			catch (const std::exception& e)
			{
				return { RenameStatus::kFailed, e.what() };
			}

			files.push_back(std::move(file));
		}

		const auto putBack = [&](size_t count) {
			for (size_t i = 0; i < count; ++i)
			{
				try
				{
					writeFileBytes(files[i].path, files[i].original, "rename");
				}
				catch (...)
				{
					// Best effort: the error already being reported is the one to act on.
				}
			}
		};

		size_t written = 0;
		try
		{
			for (; written < files.size(); ++written)
				writeFileBytes(
					files[written].path,
					rewriteReferrer(plan, files[written].type, files[written].original),
					"rename");
		}
		catch (const std::exception& e)
		{
			// files[written] is the one mid-write: its open truncated the file before the failure, so
			// it is put back along with the ones that were fully written.
			putBack(written + 1);
			return { RenameStatus::kFailed, e.what() };
		}

		std::filesystem::rename(fromPath, toPath, ec);
		if (ec)
		{
			putBack(files.size());
			return { RenameStatus::kFailed,
				     "could not rename '" + plan.from + "': " + ec.message() };
		}

		// A bake's file name is derived from its inputs (vatPathFor), so a rewritten .bvat may now
		// sit under a name the runtime will never ask for. Move each to its derived name -- what
		// keeps a rename a load, not a re-bake. Best effort, after the rename is already done: a
		// bake left behind is re-baked and later swept, never wrong.
		for (const PendingReferrer& file : files)
		{
			if (file.type != AssetType::kVat)
				continue;

			try
			{
				// Re-stamp before moving: a rename rewrites the path references inside the `.bmesh`
				// and `.banim` this was baked from, which changes their contents and so their stamps.
				// The baked tables did not change, so re-reading the inputs here is what keeps the
				// rename a load. Only now are they all in their final place.
				BVat vat            = store.Load<BVat>(store.KeyFor(file.path));
				vat.meshStamp       = stampOf(store.GetDataRoot() / vat.mesh);
				vat.skeletonStamp   = stampOf(store.GetDataRoot() / vat.skeleton);
				vat.animationsStamp = stampOf(store.GetDataRoot() / vat.animations);
				store.Save(vat, store.KeyFor(file.path));

				const std::filesystem::path derived =
					store.GetDataRoot() / vatPathFor(vat.mesh, vat.animations);
				if (!std::filesystem::equivalent(file.path, derived, ec) &&
				    !std::filesystem::exists(derived))
					std::filesystem::rename(file.path, derived, ec);
			}
			catch (const std::exception&)
			{
				// Already reported where it mattered; the orphan re-bakes.
			}
		}

		return { RenameStatus::kRenamed, {} };
	}
}
