#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/import_document.h>
#include <core/err/util.h>

#include "material_texture_refs.h"
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <core/file/file.h>

#include "fs_util.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		/** `key` re-pointed if `from` names it or holds it; nullopt when it does not. */
		std::optional<std::string>
		mapMove(const std::string& from, const std::string& to, const std::string& key)
		{
			if (key == from)
				return to;
			if (isUnder(key, from))
				return to + key.substr(from.size());

			return std::nullopt;
		}

		/** `stored`, re-pointed if it names anything the plan moves, or anything beneath it. */
		std::string
		mapTarget(const RenamePlan& plan, const std::string& stored)
		{
			if (stored.empty())
				return stored;

			const std::string key = normalizeRef(stored);
			if (const std::optional<std::string> moved =
			        mapMove(plan.subject.from, plan.subject.to, key))
				return *moved;

			if (plan.source)
				if (const std::optional<std::string> moved =
				        mapMove(plan.source->from, plan.source->to, key))
					return *moved;

			for (const RenameMove& move : plan.outputs)
				if (const std::optional<std::string> moved = mapMove(move.from, move.to, key))
					return *moved;

			return stored;
		}

		/** `key`'s file name without its extension. */
		std::string_view
		stemOf(std::string_view key)
		{
			const size_t           slash = key.find_last_of('/');
			const std::string_view name =
				slash == std::string_view::npos ? key : key.substr(slash + 1);
			return name.substr(0, name.size() - extensionOf(name).size());
		}

		/** `key` in the same directory and with the same extension, under `stem`. */
		std::string
		reStem(std::string_view key, std::string_view stem)
		{
			const size_t      slash     = key.find_last_of('/');
			const std::string directory = slash == std::string_view::npos ?
			                                  std::string() :
			                                  std::string(key.substr(0, slash + 1));
			return directory + std::string(stem) + extensionOf(key);
		}

		/**
		 * Fills in what travels with an import document: the source it describes, and each
		 * container `outputs` names after that source's stem.
		 *
		 * @throws std::runtime_error if the document is absent or will not parse -- it is what says
		 *         which containers move, so a source moved without it strands all of them -- or if
		 *         the source itself is not on disk. A missing output is ordinary and regenerable; a
		 *         missing source is a project already broken, and moving the document away from it
		 *         would leave nothing able to put it back.
		 */
		void
		planImportGroup(const std::filesystem::path& dataRoot, RenamePlan& plan)
		{
			const auto source = RenameMove{ importedSourceKeyFor(plan.subject.from),
				                            importedSourceKeyFor(plan.subject.to) };

			core::throw_runtime_error_if(
				!std::filesystem::exists(dataRoot / source.from),
				"assetlib::planRename: '{}' describes '{}', which does not exist",
				plan.subject.from,
				source.from);

			plan.source = source;

			const std::string_view was = stemOf(plan.subject.from);
			const std::string_view now = stemOf(plan.subject.to);

			const ImportDocument document = loadImportDocument(dataRoot / plan.subject.from);
			for (const std::string& output : document.outputs)
			{
				const std::string key = normalizeRef(output);

				// An output already taken off the source's stem by a rename of its own is not this
				// source's to move: its name no longer says it came from here.
				if (stemOf(key) != was)
					continue;

				plan.outputs.push_back({ key, reStem(key, now) });
			}
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

			case AssetType::kMaterial:
			{
				BMaterial material = AssetCodec<BMaterial>::Deserialize(bytes);
				mapMaterialTextures(material, [&](const std::string& key) {
					return mapTarget(plan, key);
				});
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

				document.skeleton = mapTarget(plan, document.skeleton);
				for (std::string& output : document.outputs) output = mapTarget(plan, output);
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
		auto plan         = RenamePlan();
		plan.subject.from = normalizeRef(from);
		plan.subject.to   = normalizeRef(to);

		// A `.glb` and its `.bimport` are one asset under two names, and only the document is of a
		// kind the project stores anything about -- so a source named on either side plans as the
		// document, and the source itself travels in `plan.source`.
		if (extensionOf(plan.subject.from) == c_ImportedSourceExtension)
		{
			core::throw_runtime_error_if(
				extensionOf(plan.subject.to) != c_ImportedSourceExtension,
				"assetlib::planRename: renaming '{}' to '{}' would change what kind of asset it is",
				plan.subject.from,
				plan.subject.to);

			core::throw_runtime_error_if(
				!std::filesystem::exists(graph.DataRoot() / plan.subject.from),
				"assetlib::planRename: '{}' does not exist",
				plan.subject.from);

			plan.subject.from = importDocumentKeyFor(plan.subject.from);
			plan.subject.to   = importDocumentKeyFor(plan.subject.to);
		}

		requireInsideDataRoot("assetlib::planRename", plan.subject.from);
		requireInsideDataRoot("assetlib::planRename", plan.subject.to);

		if (plan.subject.from == plan.subject.to)
			throw std::runtime_error(
				"assetlib::planRename: '" + plan.subject.from + "' is already named that");

		if (isUnder(plan.subject.to, plan.subject.from))
			throw std::runtime_error(
				"assetlib::planRename: cannot move '" + plan.subject.from + "' inside itself");

		const std::filesystem::path fromPath = graph.DataRoot() / plan.subject.from;
		const std::filesystem::path toPath   = graph.DataRoot() / plan.subject.to;

		if (!std::filesystem::exists(fromPath))
			throw std::runtime_error(
				"assetlib::planRename: '" + plan.subject.from + "' does not exist");

		if (std::filesystem::is_directory(fromPath))
		{
			for (const AssetRef& edge : graph.Edges())
				if (isUnder(edge.target, plan.subject.from))
					plan.referrers.push_back(edge);
		}
		else
		{
			plan.assetType = assetTypeFromExtension(plan.subject.from);
			if (!plan.assetType)
				throw std::runtime_error(
					"assetlib::planRename: '" + plan.subject.from +
					"' is not an asset this project stores anything about");

			if (assetTypeFromExtension(plan.subject.to) != plan.assetType)
				throw std::runtime_error(
					"assetlib::planRename: renaming '" + plan.subject.from + "' to '" +
					plan.subject.to + "' would change what kind of asset it is");

			// Its source key is derived from its own path and its outputs are named from the
			// source, so a document never moves alone: the whole import goes with it.
			if (plan.assetType == AssetType::kImportDocument)
				planImportGroup(graph.DataRoot(), plan);

			const auto follow = [&](std::string_view target) {
				const std::span<const AssetRef> held = graph.ReferrersOf(target);
				plan.referrers.insert(plan.referrers.end(), held.begin(), held.end());
			};

			follow(plan.subject.from);
			if (plan.source)
				follow(plan.source->from);
			for (const RenameMove& move : plan.outputs) follow(move.from);
		}

		// equivalent() is what tells a real collision from a case-only rename on a case-insensitive
		// filesystem, where the destination "exists" because it is the file being renamed.
		std::error_code ec;
		if (std::filesystem::exists(toPath) && !std::filesystem::equivalent(fromPath, toPath, ec))
			throw std::runtime_error(
				"assetlib::planRename: '" + plan.subject.to + "' already exists");

		// The group's destinations are held to what the subject's is, so a caller that asks before
		// confirming a rename is told here rather than after committing to it.
		const auto requireFree = [&](const RenameMove& move) {
			const std::filesystem::path was = graph.DataRoot() / move.from;
			const std::filesystem::path now = graph.DataRoot() / move.to;
			if (std::filesystem::exists(now) && !std::filesystem::equivalent(was, now, ec))
				throw std::runtime_error("assetlib::planRename: '" + move.to + "' already exists");
		};

		if (plan.source)
			requireFree(*plan.source);
		for (const RenameMove& move : plan.outputs) requireFree(move);

		if (!std::filesystem::is_directory(toPath.parent_path()))
			throw std::runtime_error(
				"assetlib::planRename: the directory to rename '" + plan.subject.from +
				"' into does not exist");

		return plan;
	}

	namespace
	{
		/** Whether two files hold the same bytes. False when either cannot be read. */
		bool
		sameContents(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			try
			{
				return std::filesystem::file_size(a) == std::filesystem::file_size(b) &&
				       core::file::read_file_bytes(a.string()) ==
				           core::file::read_file_bytes(b.string());
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
	}

	RenameResult
	AssetStore::RenameAsset(const RenamePlan& plan) const
	{
		// Unlike a deletion, a rename cannot shrug at a file that has vanished since the plan: there is
		// nothing to move, and rewriting the referrers anyway would break every one of them.
		if (!std::filesystem::exists(GetDataRoot() / plan.subject.from))
			return { RenameStatus::kFailed, "'" + plan.subject.from + "' no longer exists" };

		std::error_code ec;

		// The subject, then everything travelling with it. The `.glb` is held to what the subject
		// is: it is authored, and a reimport reads *from* it, so a rename that could not move it
		// would leave the one file here nothing can put back under neither name.
		auto steps = std::vector<RenameMove>{ { plan.subject.from, plan.subject.to } };
		if (plan.source)
		{
			if (!std::filesystem::exists(GetDataRoot() / plan.source->from))
				return { RenameStatus::kFailed, "'" + plan.source->from + "' no longer exists" };

			steps.push_back(*plan.source);
		}

		// An output is cache, and one already swept has nothing to move: the document names the new
		// path either way, so a reimport writes it where the group now lives.
		for (const RenameMove& move : plan.outputs)
			if (std::filesystem::exists(GetDataRoot() / move.from))
				steps.push_back(move);

		for (const RenameMove& step : steps)
		{
			const std::filesystem::path was = GetDataRoot() / step.from;
			const std::filesystem::path now = GetDataRoot() / step.to;

			if (std::filesystem::exists(now) && !std::filesystem::equivalent(was, now, ec) &&
			    !sameContents(was, now))
				return { RenameStatus::kFailed, "'" + step.to + "' already exists" };
		}

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
			file.path = GetDataRoot() / referrer;
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

		auto       moved        = std::vector<RenameMove>();
		const auto putBackMoved = [&] {
			for (const RenameMove& step : moved)
			{
				std::error_code undo;
				std::filesystem::rename(GetDataRoot() / step.to, GetDataRoot() / step.from, undo);
			}
		};

		for (const RenameMove& step : steps)
		{
			std::filesystem::rename(GetDataRoot() / step.from, GetDataRoot() / step.to, ec);
			if (ec)
			{
				putBackMoved();
				putBack(files.size());
				return { RenameStatus::kFailed,
					     "could not rename '" + step.from + "': " + ec.message() };
			}

			moved.push_back(step);
		}

		return { RenameStatus::kRenamed, {} };
	}
}
