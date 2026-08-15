#include <gamelib/vat_freshness.h>

#include <assetlib/bvat_io.h>
#include <assetlib/vat_bake.h>

#include <core/err/util.h>

namespace game
{
	assetlib::BVat
	EnsureVatBaked(
		const assetlib::AssetStore& store,
		std::string_view            meshRelPath,
		std::string_view            animationsRelPath)
	{
		const std::string bvatRel =
			assetlib::vatPathFor(meshRelPath, animationsRelPath).generic_string();

		// The whole store's answer and not this path's: an overlay over an archive is writable even
		// for a `.bvat` only the archive carries, and re-baking that one into the overlay is exactly
		// what an edited rig needs.
		const bool trusted = store.IsReadOnly();

		// Loaded whole before the staleness check: the fresh path needs the pixel chunks anyway,
		// so one read serves both, and only the rare stale case pays for pixels it then discards.
		if (store.Exists(bvatRel))
		{
			auto vat = store.LoadVat(bvatRel);

			// The clip-set check binds even when the stamps are not asked about: a bake of the wrong
			// clips drawn as if it were the right ones is worse than a refusal.
			const bool rightClips = assetlib::normalizePath(vat.animations) ==
			                        assetlib::normalizePath(animationsRelPath);

			if (rightClips && (trusted || !store.VatIsStale(vat)))
				return vat;
		}

		core::throw_runtime_error_if(
			trusted,
			"vat: no usable bake of '{}' in a read-only store, and there is nowhere to make one",
			bvatRel);

		auto vat = assetlib::bakeVat(
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
