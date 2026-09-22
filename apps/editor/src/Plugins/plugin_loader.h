#pragma once

#include <assetlib/AssetKindRegistry.h>
#include <editor_api/IEditorPlugin.h>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor::plugins
{
	class EditorRegistry;

	struct BuildIdentity
	{
		std::string           id;
		std::string           configuration;
		std::filesystem::path sdkStamp;
	};

	enum class PluginBinaryCopyMode
	{
		kPlatformDefault,
		kAlways,
		kNever,
	};

	/** One loaded plugin as the Plugins window shows it; `directory` is empty for the host-linked one. */
	struct LoadedPlugin
	{
		std::string           id;
		std::string           name;
		std::string           description;
		std::filesystem::path directory;
		std::filesystem::path runtimeModule;
		std::filesystem::path editorModule;
	};

	inline constexpr std::string_view c_BuiltInPluginId = "bernini.default";

	class PluginSession
	{
	public:
		~PluginSession();
		PluginSession(PluginSession&&) noexcept;
		PluginSession&
		operator=(PluginSession&&) noexcept;

		PluginSession(const PluginSession&) = delete;
		PluginSession&
		operator=(const PluginSession&) = delete;

		/**
		 * Loads every plugin in `directories`, in that order, after validating all of their
		 * descriptors against `build`. Throws naming the plugin on a descriptor that is missing,
		 * malformed, built for another engine or older than the SDK stamp, and on a kind or
		 * contribution collision between two of them.
		 */
		[[nodiscard]] static PluginSession
		Load(
			std::span<const std::filesystem::path> directories,
			const BuildIdentity&                   build,
			const std::filesystem::path&           pluginCopyRoot,
			PluginBinaryCopyMode copyMode = PluginBinaryCopyMode::kPlatformDefault,
			EditorPluginPtr      builtIn  = {});

		/** The loaded plugins' IDs, in load order; the host-linked plugin has none. */
		[[nodiscard]] const std::vector<std::string>&
		Ids() const noexcept;

		[[nodiscard]] const std::shared_ptr<assetlib::AssetKindRegistry>&
		KindRegistry() const noexcept;

		[[nodiscard]] std::span<const editor::EditorPluginPtr>
		EditorPlugins() const noexcept;

		[[nodiscard]] const EditorRegistry&
		Contributions() const noexcept;

		/** The host-linked plugin first, then the loaded ones in load order. */
		[[nodiscard]] std::span<const LoadedPlugin>
		Plugins() const noexcept;

	private:
		PluginSession();

		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	[[nodiscard]] BuildIdentity
	CurrentBuildIdentity();

	[[nodiscard]] std::filesystem::path
	DefaultPluginCopyRoot();

	/** `plugins/` beside the editor executable: every subdirectory holding a descriptor is loaded. */
	[[nodiscard]] std::filesystem::path
	DefaultPluginRoot();

	/** The subdirectories of `root` that hold a descriptor, sorted by name; none when `root` is absent. */
	[[nodiscard]] std::vector<std::filesystem::path>
	DiscoverPluginDirectories(const std::filesystem::path& root);

	/** Directories named by `pluginDirectories` in the editor config, for a plugin built elsewhere. */
	[[nodiscard]] std::vector<std::filesystem::path>
	ConfiguredPluginDirectories(const std::filesystem::path& configPath);

	/** The IDs in `required` that `loaded` lacks, in `required` order. */
	[[nodiscard]] std::vector<std::string>
	MissingRequiredPlugins(
		std::span<const std::string> loaded,
		std::span<const std::string> required);
}
