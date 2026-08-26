#pragma once

namespace assetlib
{
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

		// The `.ktx2` files a re-extract left behind, sorted. Reported, never removed -- see
		// TextureRefresh.
		std::vector<std::string> supersededTextures;

		[[nodiscard]] size_t
		Count(MigratedFile::Outcome outcome) const noexcept;
	};
}
