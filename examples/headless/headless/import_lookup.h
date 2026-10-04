#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/import_document.h>
#include <cstdint>
#include <filesystem>
#include <gamelib/ClipInfo.h>
#include <string>
#include <string_view>
#include <vector>

namespace headless
{
	/**
	 * @throws std::runtime_error naming `key` and the command that writes it back, when a derived
	 *         container an import names is not on disk.
	 */
	void
	RequireDerived(
		const assetlib::AssetStore&  store,
		std::string_view             key,
		const std::filesystem::path& dataRoot);

	/** The `.bimport` a `--import` names. @throws std::runtime_error if it is neither a `.bimport` nor a `.glb`. */
	[[nodiscard]] std::string
	ImportDocumentKey(std::string_view key);

	/** The clip set an import cooks, or empty when it cooks none. */
	[[nodiscard]] std::string
	AnimationOutput(const assetlib::ImportDocument& document);

	/**
	 * The index of the clip named `name`, or 0 when `name` is empty.
	 * @throws std::runtime_error listing every clip when none is named `name`.
	 */
	[[nodiscard]] uint32_t
	FindClip(const std::vector<game::ClipInfo>& clips, std::string_view name);
}
