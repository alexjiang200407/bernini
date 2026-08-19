#include <gamelib/vat_freshness.h>

#include <assetlib/bvat_io.h>
#include <assetlib/vat_bake.h>

#include <core/err/util.h>

namespace game
{
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
			assetlib::BVat vat = store.LoadVat(bvatRel);

			if (assetlib::normalizePath(vat.animations) !=
			    assetlib::normalizePath(animationsRelPath))
			{
				return VatBakeState::kOtherClips;
			}

			// See EnsureVatBaked: a read-only store's bakes are pack's output and trusted.
			if (!store.IsReadOnly() && store.VatIsStale(vat))
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

		vat = assetlib::bakeVat(
			store,
			assetlib::VatBakeDesc{ std::string(meshRelPath), std::string(animationsRelPath) });

		// The writable layer may hold nothing yet -- an overlay over an archive starts empty -- so
		// the directory the bake lands in is made rather than assumed. Named here rather than left
		// to saveVat, which would blame the file for a directory that was never made.
		const std::filesystem::path bvatAbs = store.GetDataRoot() / bvatRel;

		std::error_code ec;
		std::filesystem::create_directories(bvatAbs.parent_path(), ec);
		core::throw_runtime_error_if(
			static_cast<bool>(ec),
			"vat: cannot make '{}' to bake into: {}",
			bvatAbs.parent_path().string(),
			ec.message());

		assetlib::saveVat(vat, bvatAbs);
		return vat;
	}
}
