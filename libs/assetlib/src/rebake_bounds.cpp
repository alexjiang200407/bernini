#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/rebake_bounds.h>
#include <assetlib/skinning.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include <core/log/ScopedStage.h>

namespace assetlib
{
	namespace
	{
		namespace fs = std::filesystem;

		/** One box a bake writes: which source content, for which mesh entry. */
		struct BoxKey
		{
			uint64_t sourceSignature;
			uint32_t meshIndex;

			friend auto
			operator<=>(const BoxKey&, const BoxKey&) = default;
		};

		std::vector<std::string>
		containersUnder(const fs::path& dataRoot, std::string_view extension)
		{
			auto out = std::vector<std::string>();

			std::error_code ec;
			const auto      walk = fs::directory_options::skip_permission_denied;
			for (const fs::directory_entry& entry :
			     fs::recursive_directory_iterator(dataRoot, walk, ec))
			{
				if (entry.is_regular_file(ec) && entry.path().extension() == extension)
					out.push_back(
						normalizePath(fs::relative(entry.path(), dataRoot).generic_string()));
			}

			// Directory order is unspecified; a report that reshuffled between runs would read as
			// a change where there is none.
			std::ranges::sort(out);
			return out;
		}
	}

	size_t
	RebakeBoundsReport::Count(const RebakedFile::Outcome outcome) const noexcept
	{
		return static_cast<size_t>(std::ranges::count(files, outcome, &RebakedFile::outcome));
	}

	RebakeBoundsReport
	AssetStore::RebakePosedBounds(const bool dryRun) const
	{
		// Walked before the stage opens, so the line can say what the run is over: this is the one
		// stage whose cost scales with the whole project rather than with one rig.
		const std::vector<std::string> meshPaths = containersUnder(GetDataRoot(), c_MeshExtension);
		const std::vector<std::string> animPaths =
			containersUnder(GetDataRoot(), c_AnimationExtension);

		const auto stage = core::logging::ScopedStage(
			"assetlib rebake posed bounds: {} meshes, {} clip sets{}",
			meshPaths.size(),
			animPaths.size(),
			dryRun ? " (dry run)" : "");

		auto report = RebakeBoundsReport();

		// Loaded once and kept for the run: a rig's meshes are consulted by every one of its clip
		// sets, and re-reading megabytes of vertex data per `.banim` would make even the dry run
		// cost minutes. A project's meshes fit in memory the way its textures would not.
		auto skeletons = std::unordered_map<std::string, Skeleton>();
		auto meshes    = std::unordered_map<std::string, BMesh>();

		// Plain loads, not the regeneration seam: a retrofit stamps boxes onto the bytes a
		// project actually holds, and a stale group is migrate's job -- a foreign token here
		// refuses loudly rather than baking boxes for a mesh the disk does not carry.
		// `.bskel` path -> the rig, loaded once.
		const auto skeletonAt = [&](const std::string& path) -> const Skeleton& {
			const auto it = skeletons.find(path);
			return it != skeletons.end() ?
			           it->second :
			           skeletons.emplace(path, Load<Skeleton>(path)).first->second;
		};
		const auto meshAt = [&](const std::string& path) -> const BMesh& {
			const auto it = meshes.find(path);
			return it != meshes.end() ? it->second :
			                            meshes.emplace(path, Load<BMesh>(path)).first->second;
		};

		// posedBoundsSignature hashes a mesh's whole vertex blob, so it too is computed once per
		// (mesh, rig) pair rather than once per clip set.
		auto       signatures  = std::unordered_map<std::string, uint64_t>();
		const auto signatureAt = [&](const std::string& meshPath, const std::string& rigPath) {
			const std::string key = meshPath + '\n' + rigPath;
			const auto        it  = signatures.find(key);
			return it != signatures.end() ?
			           it->second :
			           signatures
			               .emplace(
							   key,
							   posedBoundsSignature(meshAt(meshPath), skeletonAt(rigPath)))
			               .first->second;
		};

		// Which meshes skin to which rig. Paired by skeleton *signature*, not path: the runtime
		// accepts any clip set whose signature matches the mesh's rig (animationsMatchSkeleton),
		// and one rig routinely exists under more than one path -- assetlib_cli bake names a
		// `.bskel` after every mesh it cooks.
		auto meshesBySkeleton = std::unordered_map<uint64_t, std::vector<std::string>>();
		for (const std::string& meshPath : meshPaths)
		{
			try
			{
				const MeshRefs refs = LoadMeshRefs(meshPath);
				if (refs.skeleton.empty())
					continue;

				meshesBySkeleton[skeletonSignature(skeletonAt(normalizePath(refs.skeleton)))]
					.push_back(meshPath);
			}
			catch (const std::exception& e)
			{
				// An unreadable mesh or rig would make every clip set of that rig look orphaned
				// below, so it is a failure here rather than a skip.
				report.files.push_back(
					{ GetDataRoot() / meshPath, RebakedFile::Outcome::kFailed, e.what() });
			}
		}

		for (const std::string& animPath : animPaths)
		{
			RebakedFile& entry = report.files.emplace_back(
				RebakedFile{ GetDataRoot() / animPath, RebakedFile::Outcome::kFailed, {} });

			try
			{
				AnimationSet animations = Load<AnimationSet>(animPath);

				const auto paired = meshesBySkeleton.find(animations.skeletonSignature);
				if (paired == meshesBySkeleton.end())
				{
					entry.outcome = RebakedFile::Outcome::kOrphaned;
					continue;
				}

				const std::string rigPath  = normalizePath(animations.skeleton);
				const Skeleton&   skeleton = skeletonAt(rigPath);

				// The keys the bake would write. Boxes are keyed by content, so "already current"
				// is exactly "every key is stored" -- and a stored key no mesh produces any more
				// is a source that changed since, worth clearing out.
				auto wanted = std::vector<BoxKey>();
				for (const std::string& meshPath : paired->second)
				{
					const BMesh&   mesh      = meshAt(meshPath);
					const uint64_t signature = signatureAt(meshPath, rigPath);
					for (uint32_t meshIndex = 0; meshIndex < mesh.meshes.size(); ++meshIndex)
						if (isSkinned(mesh, meshIndex))
							wanted.push_back(BoxKey{ signature, meshIndex });
				}

				auto stored = std::vector<BoxKey>();
				stored.reserve(animations.posedBoxes.size());
				for (const PosedBox& box : animations.posedBoxes)
					stored.push_back(BoxKey{ box.sourceSignature, box.meshIndex });

				// Two identical meshes want identical keys; the bake stores one box for both.
				std::ranges::sort(wanted);
				const auto duplicates = std::ranges::unique(wanted);
				wanted.erase(duplicates.begin(), duplicates.end());
				std::ranges::sort(stored);

				if (wanted == stored)
				{
					entry.outcome = RebakedFile::Outcome::kCurrent;
					continue;
				}

				if (!dryRun)
				{
					animations.posedBoxes.clear();
					for (const std::string& meshPath : paired->second)
						bakePosedBounds(animations, meshAt(meshPath), skeleton);

					// A skin the bake refuses stays boxless and would land here every run; only a
					// rewrite that changes bytes is worth dirtying a version-controlled binary for.
					if (AssetCodec<AnimationSet>::Serialize(animations) ==
					    GetFiles().Read(animPath))
					{
						entry.outcome = RebakedFile::Outcome::kCurrent;
						continue;
					}

					Save(animations, animPath);
				}
				entry.outcome = RebakedFile::Outcome::kRebaked;
			}
			catch (const std::exception& e)
			{
				entry.message = e.what();
			}
		}

		return report;
	}
}
