#include <gamelib/vat_freshness.h>

#include <assetlib/vat_bake.h>
#include <assetlib_structs/SourceRef.h>

#include <core/err/util.h>

namespace game
{
	namespace
	{
		/**
		 * One bake this process made from the seam's outputs: the written `.bvat`'s own stamp,
		 * and the source state of each group it was baked from. Both halves matter -- the stamp
		 * says nothing rewrote the file, the sources say the groups have not moved since, so a
		 * re-exported `.glb` or an edited document forfeits the trust even though the bake's
		 * derived-file stamps still hold.
		 */
		struct SeamBake
		{
			core::file::FileStamp stamp;
			assetlib::SourceRef   mesh;
			assetlib::SourceRef   skeleton;
			assetlib::SourceRef   animations;
		};

		/**
		 * The bakes this process made, keyed by project and container. A group that is a cache
		 * miss on disk stays one until `migrate` rewrites it, so without this the answer a bake
		 * just made true would read stale forever -- the editor's bake offer would loop, and
		 * every acquire would pay the bake again. The stamp is size + mtime seconds, so a foreign
		 * write inside the same second at the same size escapes it -- narrow, and in a checkout's
		 * favour: a sibling's bake arrives with its own write time.
		 */
		std::mutex                                g_SeamBakesMutex;
		std::unordered_map<std::string, SeamBake> g_SeamBakes;

		std::string
		SeamBakeKey(const assetlib::AssetStore& store, const std::string& bvatRel)
		{
			return store.GetDataRoot().lexically_normal().generic_string() + '|' + bvatRel;
		}

		bool
		IsSeamBake(
			const assetlib::AssetStore& store,
			const std::string&          bvatRel,
			const assetlib::BVat&       vat)
		{
			SeamBake recorded;
			{
				const std::lock_guard lock(g_SeamBakesMutex);
				const auto            found = g_SeamBakes.find(SeamBakeKey(store, bvatRel));
				if (found == g_SeamBakes.end())
					return false;
				recorded = found->second;
			}

			const std::optional<core::file::FileStamp> stamp = store.GetFiles().Stat(bvatRel);
			return stamp.has_value() && recorded.stamp == *stamp &&
			       recorded.mesh == store.GeometryGroupSource(vat.mesh) &&
			       recorded.skeleton == store.GeometryGroupSource(vat.skeleton) &&
			       recorded.animations == store.GeometryGroupSource(vat.animations);
		}

		void
		RecordSeamBake(
			const assetlib::AssetStore& store,
			const std::string&          bvatRel,
			const assetlib::BVat&       vat)
		{
			const std::optional<core::file::FileStamp> stamp = store.GetFiles().Stat(bvatRel);
			if (!stamp.has_value())
			{
				// The mount cannot see the file the data root just took, so the bake cannot be
				// remembered -- the next ask re-bakes, loudly beats silently.
				logger::warn("vat: baked '{}' but the mount does not serve it", bvatRel);
				return;
			}

			SeamBake bake{
				*stamp,
				store.GeometryGroupSource(vat.mesh),
				store.GeometryGroupSource(vat.skeleton),
				store.GeometryGroupSource(vat.animations),
			};

			const std::lock_guard lock(g_SeamBakesMutex);
			g_SeamBakes[SeamBakeKey(store, bvatRel)] = std::move(bake);
		}
	}

	VatBakeState
	VatFreshness(
		const assetlib::AssetStore& store,
		std::string_view            meshRelPath,
		std::string_view            animationsRelPath,
		assetlib::BVat*             out)
	{
		const std::string bvatRel =
			assetlib::vatPathFor(meshRelPath, animationsRelPath).generic_string();

		if (!store.Exists(bvatRel))
			return VatBakeState::kMissing;

		try
		{
			assetlib::BVat vat = store.Load<assetlib::BVat>(bvatRel);

			if (assetlib::normalizePath(vat.animations) !=
			    assetlib::normalizePath(animationsRelPath))
			{
				return VatBakeState::kOtherClips;
			}

			// See EnsureVatBaked: a read-only store's bakes are pack's output and trusted.
			if (!store.IsReadOnly() && store.VatIsStale(vat))
				return VatBakeState::kStale;

			// The group axis: geometry regenerated in memory leaves the disk stamps this bake
			// recorded untouched, so a `.bvat` whose group is a cache miss is itself stale
			// however well its own stamps hold -- unless it is the bake this process made from
			// the seam's outputs, current until its groups' sources move again.
			if (!store.IsReadOnly() && !IsSeamBake(store, bvatRel, vat) &&
			    (store.GeometryIsStale(vat.mesh) || store.GeometryIsStale(vat.skeleton) ||
			     store.GeometryIsStale(vat.animations)))
				return VatBakeState::kStale;

			if (out != nullptr)
				*out = std::move(vat);

			return VatBakeState::kFresh;
		}
		catch (const std::exception&)
		{
			// One that will not parse is one that is not there: wholly derived, so re-baking beats
			// reporting a container error for a file nobody authored.
			return VatBakeState::kMissing;
		}
	}

	assetlib::BVat
	EnsureVatBaked(
		const assetlib::AssetStore& store,
		std::string_view            meshRelPath,
		std::string_view            animationsRelPath)
	{
		const std::string bvatRel =
			assetlib::vatPathFor(meshRelPath, animationsRelPath).generic_string();

		// Through the same door the editor asks with, and taking what it parsed: the rule lives in
		// one place, and the fresh path still costs one read.
		auto vat = assetlib::BVat();
		if (VatFreshness(store, meshRelPath, animationsRelPath, &vat) == VatBakeState::kFresh)
			return vat;

		// The whole store's answer and not this path's: an overlay over an archive is writable even
		// for a `.bvat` only the archive carries, and re-baking that one into the overlay is exactly
		// what an edited rig needs.
		core::throw_runtime_error_if(
			store.IsReadOnly(),
			"vat: no usable bake of '{}' in a read-only store, and there is nowhere to make one",
			bvatRel);

		vat = store.BakeVat(
			assetlib::VatBakeDesc{ std::string(meshRelPath), std::string(animationsRelPath) });

		// The writable layer may hold nothing yet -- an overlay over an archive starts empty -- and
		// Save makes what the key names.
		store.Save(vat, bvatRel);
		RecordSeamBake(store, bvatRel, vat);
		return vat;
	}
}
