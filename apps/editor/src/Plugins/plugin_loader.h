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

	enum class ContributionKind
	{
		kAssetKind,
		kMenu,
		kPanel,
		kAssetEditor,
		kAction,
		kImporter,
		kThumbnailProvider,
	};

	struct LoadedContribution
	{
		ContributionKind kind;
		std::string      id;
		std::string      detail;
	};

	/** What one plugin put into the session; `directory` is empty for the host-linked plugin. */
	struct LoadedPlugin
	{
		std::string                     id;
		std::filesystem::path           directory;
		std::filesystem::path           runtimeModule;
		std::filesystem::path           editorModule;
		std::vector<LoadedContribution> contributions;
	};

	/** A descriptor read from a configured directory, whether or not the project required it. */
	struct ConfiguredPlugin
	{
		std::string           id;
		std::filesystem::path directory;
		bool                  loaded = false;
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

		[[nodiscard]] static PluginSession
		Load(
			std::span<const std::string>           requiredIds,
			std::span<const std::filesystem::path> configuredDirectories,
			const BuildIdentity&                   build,
			const std::filesystem::path&           pluginCopyRoot,
			PluginBinaryCopyMode copyMode = PluginBinaryCopyMode::kPlatformDefault,
			EditorPluginPtr      builtIn  = {});

		[[nodiscard]] const std::vector<std::string>&
		Ids() const noexcept;

		[[nodiscard]] const std::shared_ptr<assetlib::AssetKindRegistry>&
		KindRegistry() const noexcept;

		[[nodiscard]] std::span<const editor::EditorPluginPtr>
		EditorPlugins() const noexcept;

		[[nodiscard]] const EditorRegistry&
		Contributions() const noexcept;

		/** The host-linked plugin first, then the required ones in project order. */
		[[nodiscard]] std::span<const LoadedPlugin>
		Plugins() const noexcept;

		/** Every configured directory's descriptor, in configuration order. */
		[[nodiscard]] std::span<const ConfiguredPlugin>
		Configured() const noexcept;

	private:
		PluginSession();

		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	[[nodiscard]] BuildIdentity
	CurrentBuildIdentity();

	[[nodiscard]] std::filesystem::path
	DefaultPluginCopyRoot();

	[[nodiscard]] std::vector<std::filesystem::path>
	ConfiguredPluginDirectories(const std::filesystem::path& configPath);

	[[nodiscard]] bool
	OpeningNeedsPluginRelaunch(
		std::span<const std::string> loaded,
		std::span<const std::string> requested);
}
