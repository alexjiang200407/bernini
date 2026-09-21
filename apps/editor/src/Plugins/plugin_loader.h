#pragma once

#include <assetlib/AssetKindRegistry.h>
#include <editor_api/IEditorPlugin.h>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
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
			PluginBinaryCopyMode copyMode = PluginBinaryCopyMode::kPlatformDefault);

		[[nodiscard]] const std::vector<std::string>&
		Ids() const noexcept;

		[[nodiscard]] const std::shared_ptr<assetlib::AssetKindRegistry>&
		KindRegistry() const noexcept;

		[[nodiscard]] std::span<const editor::EditorPluginPtr>
		EditorPlugins() const noexcept;

		[[nodiscard]] const EditorRegistry&
		Contributions() const noexcept;

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
