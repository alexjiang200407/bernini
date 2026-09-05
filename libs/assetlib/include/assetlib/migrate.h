#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
namespace assetlib
{
	struct MovedTexture;

	/** What happened to one file under Migrate. */
	struct MigratedFile
	{
		enum class Outcome
		{
			kUnchanged,  // already what the current serializer writes
			kRewritten,  // not byte-identical to the current form; re-saved -- or would be, on a dry run
			kFailed  // could not be read or converted; `message` says why
		};

		std::filesystem::path path;
		Outcome               outcome;
		std::string           message;
	};

	struct MigrateReport
	{
		std::vector<MigratedFile> files;

		// The `.ktx2` files a re-extract left behind and could not account for, sorted. Reported,
		// never removed -- see TextureRefresh.
		std::vector<std::string> supersededTextures;

		// The ones it could: the same bytes under the name the current rule gives them, followed so
		// the materials routing at them came along. See TextureRefresh::moved.
		std::vector<MovedTexture> movedTextures;

		/**
		 * `<material>: <texture>` for every texture a material names that is not on disk, sorted.
		 *
		 * Nothing here can put one back -- a material's routes are authored, and which file the
		 * author meant is not derivable once it is gone. Reported because the alternative is a
		 * material that silently draws untextured, which is indistinguishable from one authored
		 * that way.
		 */
		std::vector<std::string> danglingTextures;

		[[nodiscard]] size_t
		Count(MigratedFile::Outcome outcome) const noexcept;
	};
}
