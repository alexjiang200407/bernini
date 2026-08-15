#include <gamelib/vat_freshness.h>

#include <assetlib/bvat_io.h>
#include <assetlib/vat_bake.h>

#include <core/file/LooseFileSystem.h>

namespace game
{
	assetlib::BVat
	EnsureVatBaked(
		const std::filesystem::path& dataRoot,
		std::string_view             meshRelPath,
		std::string_view             animationsRelPath)
	{
		const auto bvatAbs = dataRoot / assetlib::vatPathFor(meshRelPath, animationsRelPath);

		// Loaded whole before the staleness check: the fresh path needs the pixel chunks anyway,
		// so one read serves both, and only the rare stale case pays for pixels it then discards.
		std::error_code ec;
		if (std::filesystem::exists(bvatAbs, ec))
		{
			auto vat = assetlib::loadVat(bvatAbs);
			if (!assetlib::vatIsStale(vat, core::file::LooseFileSystem(dataRoot)) &&
			    assetlib::normalizePath(vat.animations) ==
			        assetlib::normalizePath(animationsRelPath))
				return vat;
		}

		auto vat = assetlib::bakeVat(
			assetlib::VatBakeDesc{ dataRoot,
		                           std::string(meshRelPath),
		                           std::string(animationsRelPath) });
		assetlib::saveVat(vat, bvatAbs);
		return vat;
	}
}
