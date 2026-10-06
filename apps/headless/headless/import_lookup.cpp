#include <assetlib/asset_refs.h>
#include <core/err/util.h>
#include <cstddef>
#include <format>
#include <headless/import_lookup.h>
#include <system_error>

namespace headless
{
	namespace
	{
		/** The .bproj beside the data root, for a message that has to name one. */
		std::string
		ProjectFileHint(const std::filesystem::path& dataRoot)
		{
			auto root = std::filesystem::absolute(dataRoot).lexically_normal();
			if (!root.has_filename())
				root = root.parent_path();

			const std::filesystem::path projectDir = root.parent_path();
			std::error_code             error;
			for (const auto& entry : std::filesystem::directory_iterator(projectDir, error))
			{
				if (entry.path().extension() == ".bproj")
					return entry.path().string();
			}
			return (projectDir / "<project>.bproj").string();
		}
	}

	void
	RequireDerived(
		const assetlib::AssetStore&  store,
		const std::string_view       key,
		const std::filesystem::path& dataRoot)
	{
		if (!store.Exists(key))
		{
			core::throw_runtime_error(
				"{} is not on disk. It is a derived container, which a project does not commit: "
				"`assetlib_cli migrate --project \"{}\" --yes` writes back every one its .bimport "
				"documents name.",
				key,
				ProjectFileHint(dataRoot));
		}
	}

	std::string
	ImportDocumentKey(const std::string_view key)
	{
		if (key.ends_with(".glb"))
			return assetlib::importDocumentKeyFor(key);

		if (!key.ends_with(".bimport"))
		{
			core::throw_runtime_error("--import {} names neither a .bimport nor a .glb", key);
		}
		return std::string(key);
	}

	std::string
	AnimationOutput(const assetlib::ImportDocument& document)
	{
		for (const std::string& output : document.outputs)
		{
			if (assetlib::assetTypeFromExtension(output) == assetlib::AssetType::kAnimation)
				return output;
		}
		return {};
	}

	uint32_t
	FindClip(const std::vector<game::ClipInfo>& clips, const std::string_view name)
	{
		if (name.empty())
			return 0;

		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (clips[i].name == name)
				return static_cast<uint32_t>(i);
		}

		std::string names;
		for (const game::ClipInfo& clip : clips)
		{
			names += std::format("\n  {}", clip.name);
		}
		core::throw_runtime_error("--clip {} is not in the clip set, which holds:{}", name, names);
	}
}
