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
	struct BuildIdentity
	{
		std::string           id;
		std::string           configuration;
		std::filesystem::path sdkStamp;
	};

	enum class ShadowCopy
	{
		kPlatformDefault,
		kAlways,
		kNever,
	};

	class PluginSession
	{
	public:
		PluginSession();
		~PluginSession();
		PluginSession(PluginSession&&) noexcept;
		PluginSession&
		operator=(PluginSession&&) noexcept;

		PluginSession(const PluginSession&) = delete;
		PluginSession&
		operator=(const PluginSession&) = delete;

		[[nodiscard]] const std::vector<std::string>&
		Ids() const noexcept;

		[[nodiscard]] const std::shared_ptr<assetlib::AssetKindRegistry>&
		Kinds() const noexcept;

		[[nodiscard]] std::span<const editor::EditorPluginPtr>
		EditorPlugins() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

		friend PluginSession
		LoadPluginSession(
			std::span<const std::string>,
			std::span<const std::filesystem::path>,
			const BuildIdentity&,
			const std::filesystem::path&,
			ShadowCopy);
	};

	[[nodiscard]] BuildIdentity
	CurrentBuildIdentity();

	[[nodiscard]] std::filesystem::path
	DefaultShadowRoot();

	[[nodiscard]] std::vector<std::filesystem::path>
	ConfiguredPluginDirectories(const std::filesystem::path& configPath);

	[[nodiscard]] PluginSession
	LoadPluginSession(
		std::span<const std::string>           requiredIds,
		std::span<const std::filesystem::path> configuredDirectories,
		const BuildIdentity&                   build,
		const std::filesystem::path&           shadowRoot,
		ShadowCopy                             shadowCopy = ShadowCopy::kPlatformDefault);

	[[nodiscard]] bool
	OpeningNeedsPluginRelaunch(
		std::span<const std::string> loaded,
		std::span<const std::string> requested);
}
