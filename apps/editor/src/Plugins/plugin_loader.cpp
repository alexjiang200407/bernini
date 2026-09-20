#include "Plugins/plugin_loader.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QString>

#include <algorithm>
#include <assetlib/AssetKindRegistry.h>
#include <assetlib/IAssetPlugin.h>
#include <core/platform/util.h>
#include <cstdint>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/PluginDescriptor.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace editor::plugins
{
	namespace
	{
		struct Descriptor
		{
			std::string                        id;
			std::string                        engineBuildId;
			std::string                        configuration;
			std::filesystem::path              directory;
			std::filesystem::path              runtime;
			std::filesystem::path              editor;
			std::vector<std::filesystem::path> dependencies;
		};

		bool
		IsPluginId(const std::string_view id)
		{
			bool componentStart = true;
			bool sawDot         = false;
			for (const char c : id)
			{
				if (componentStart)
				{
					if (c < 'a' || c > 'z')
						return false;
					componentStart = false;
					continue;
				}
				if (c == '.')
				{
					componentStart = true;
					sawDot         = true;
					continue;
				}
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
					return false;
			}
			return sawDot && !componentStart;
		}

		bool
		IsContainedRelativePath(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute())
				return false;
			for (const auto& component : path)
				if (component == "..")
					return false;
			return true;
		}

		Descriptor
		ReadDescriptor(const std::filesystem::path& directory)
		{
			const std::filesystem::path file = directory / editor::c_PluginDescriptorFileName;
			std::ifstream               stream(file);
			if (!stream)
				throw std::runtime_error("Plugin descriptor is missing: " + file.string());

			try
			{
				const nlohmann::json json = nlohmann::json::parse(stream);
				if (json.at("version").get<uint32_t>() != editor::c_PluginDescriptorVersion)
					throw std::runtime_error(
						"Plugin descriptor version is unsupported: " + file.string());

				Descriptor descriptor;
				descriptor.id            = json.at("id").get<std::string>();
				descriptor.engineBuildId = json.at("engineBuildId").get<std::string>();
				descriptor.configuration = json.at("configuration").get<std::string>();
				descriptor.directory     = directory;
				descriptor.runtime       = json.value("runtime", std::filesystem::path());
				descriptor.editor        = json.value("editor", std::filesystem::path());
				descriptor.dependencies =
					json.value("dependencies", std::vector<std::filesystem::path>());

				if (!IsPluginId(descriptor.id) ||
				    (descriptor.runtime.empty() && descriptor.editor.empty()))
					throw std::runtime_error("Plugin descriptor is incomplete: " + file.string());

				for (const std::filesystem::path& path : descriptor.dependencies)
					if (!IsContainedRelativePath(path))
						throw std::runtime_error(
							"Plugin dependency path escapes its directory: " + path.string());
				for (const std::filesystem::path& path : { descriptor.runtime, descriptor.editor })
					if (!path.empty() && !IsContainedRelativePath(path))
						throw std::runtime_error(
							"Plugin module path escapes its directory: " + path.string());

				return descriptor;
			}
			catch (const nlohmann::json::exception& error)
			{
				throw std::runtime_error(
					"Malformed plugin descriptor " + file.string() + ": " + error.what());
			}
		}

		std::vector<std::filesystem::path>
		FilesOf(const Descriptor& descriptor)
		{
			std::vector<std::filesystem::path> files = descriptor.dependencies;
			if (!descriptor.runtime.empty())
				files.push_back(descriptor.runtime);
			if (!descriptor.editor.empty() && descriptor.editor != descriptor.runtime)
				files.push_back(descriptor.editor);
			return files;
		}

		void
		ValidateDescriptor(const Descriptor& descriptor, const BuildIdentity& build)
		{
			if (descriptor.engineBuildId != build.id)
				throw std::runtime_error(
					"Plugin " + descriptor.id + " was built for engine " +
					descriptor.engineBuildId + ", not " + build.id);
			if (descriptor.configuration != build.configuration)
				throw std::runtime_error(
					"Plugin " + descriptor.id + " was built as " + descriptor.configuration +
					", not " + build.configuration);

			std::error_code ec;
			const auto      engineWrite = std::filesystem::last_write_time(build.sdkStamp, ec);
			if (ec)
				throw std::runtime_error("Editor SDK stamp is missing: " + build.sdkStamp.string());

			for (const std::filesystem::path& relative : FilesOf(descriptor))
			{
				const std::filesystem::path file = descriptor.directory / relative;
				if (!std::filesystem::is_regular_file(file, ec) || ec)
					throw std::runtime_error("Plugin dependency is missing: " + file.string());
				const auto pluginWrite = std::filesystem::last_write_time(file, ec);
				if (ec || pluginWrite < engineWrite)
					throw std::runtime_error(
						"Plugin " + descriptor.id +
						" is older than this editor SDK: " + file.string());
			}
		}

		bool
		ShouldShadow(const ShadowCopy choice)
		{
			if (choice == ShadowCopy::kAlways)
				return true;
			if (choice == ShadowCopy::kNever)
				return false;
#if defined(_WIN32)
			return true;
#else
			return false;
#endif
		}

		Descriptor
		PrepareDescriptor(
			const Descriptor&            descriptor,
			const std::filesystem::path& shadowRoot,
			ShadowCopy                   shadowCopy)
		{
			if (!ShouldShadow(shadowCopy))
				return descriptor;

			Descriptor prepared = descriptor;
			prepared.directory  = shadowRoot / descriptor.id;
			std::error_code ec;
			std::filesystem::remove_all(prepared.directory, ec);
			std::filesystem::create_directories(prepared.directory, ec);
			if (ec)
				throw std::runtime_error(
					"Cannot create plugin shadow directory: " + prepared.directory.string());

			for (const std::filesystem::path& relative : FilesOf(descriptor))
			{
				const std::filesystem::path destination = prepared.directory / relative;
				std::filesystem::create_directories(destination.parent_path(), ec);
				std::filesystem::copy_file(
					descriptor.directory / relative,
					destination,
					std::filesystem::copy_options::overwrite_existing,
					ec);
				if (ec)
					throw std::runtime_error("Cannot shadow plugin file: " + destination.string());
			}
			return prepared;
		}
	}

	struct PluginSession::Impl
	{
		std::vector<std::unique_ptr<QLibrary>>       modules;
		std::vector<assetlib::AssetPluginPtr>        assetPlugins;
		std::vector<editor::EditorPluginPtr>         editorPlugins;
		std::shared_ptr<assetlib::AssetKindRegistry> kinds =
			std::make_shared<assetlib::AssetKindRegistry>();
		std::vector<std::string> ids;
	};

	PluginSession::PluginSession() : m_Impl(std::make_unique<Impl>()) {}
	PluginSession::~PluginSession()                        = default;
	PluginSession::PluginSession(PluginSession&&) noexcept = default;
	PluginSession&
	PluginSession::operator=(PluginSession&&) noexcept = default;

	const std::vector<std::string>&
	PluginSession::Ids() const noexcept
	{
		return m_Impl->ids;
	}

	const std::shared_ptr<assetlib::AssetKindRegistry>&
	PluginSession::Kinds() const noexcept
	{
		return m_Impl->kinds;
	}

	std::span<const editor::EditorPluginPtr>
	PluginSession::EditorPlugins() const noexcept
	{
		return m_Impl->editorPlugins;
	}

	BuildIdentity
	CurrentBuildIdentity()
	{
		return { BERNINI_EDITOR_BUILD_ID,
			     BERNINI_EDITOR_BUILD_CONFIGURATION,
			     BERNINI_EDITOR_SDK_STAMP };
	}

	std::filesystem::path
	DefaultShadowRoot()
	{
		return std::filesystem::temp_directory_path() / "bernini-editor-plugins" /
		       std::to_string(QCoreApplication::applicationPid());
	}

	std::vector<std::filesystem::path>
	ConfiguredPluginDirectories(const std::filesystem::path& configPath)
	{
		std::ifstream stream(configPath);
		if (!stream)
			throw std::runtime_error("Cannot open editor config: " + configPath.string());
		try
		{
			const nlohmann::json               json = nlohmann::json::parse(stream);
			std::vector<std::filesystem::path> directories;
			for (const std::string& path :
			     json.value("pluginDirectories", std::vector<std::string>()))
			{
				directories.emplace_back(core::expand_home(path));
			}
			return directories;
		}
		catch (const nlohmann::json::exception& error)
		{
			throw std::runtime_error("Malformed editor config: " + std::string(error.what()));
		}
	}

	PluginSession
	LoadPluginSession(
		std::span<const std::string>           requiredIds,
		std::span<const std::filesystem::path> configuredDirectories,
		const BuildIdentity&                   build,
		const std::filesystem::path&           shadowRoot,
		ShadowCopy                             shadowCopy)
	{
		std::map<std::string, Descriptor, std::less<>> available;
		for (const std::filesystem::path& directory : configuredDirectories)
		{
			Descriptor descriptor = ReadDescriptor(directory);
			if (!available.emplace(descriptor.id, std::move(descriptor)).second)
				throw std::runtime_error("Plugin ID has more than one configured directory");
		}

		std::vector<Descriptor>            selected;
		std::set<std::string, std::less<>> seen;
		for (const std::string& id : requiredIds)
		{
			if (!seen.emplace(id).second)
				throw std::runtime_error("Project requires plugin more than once: " + id);
			const auto found = available.find(id);
			if (found == available.end())
				throw std::runtime_error("Project requires an unconfigured plugin: " + id);
			ValidateDescriptor(found->second, build);
			selected.push_back(found->second);
		}

		PluginSession                              session;
		std::map<std::filesystem::path, QLibrary*> loadedModules;
		const auto loadModule = [&](const std::filesystem::path& path) -> QLibrary& {
			const std::filesystem::path normalized = path.lexically_normal();
			if (const auto found = loadedModules.find(normalized); found != loadedModules.end())
				return *found->second;

			auto module = std::make_unique<QLibrary>(QString::fromStdWString(normalized.wstring()));
			module->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
			if (!module->load())
				throw std::runtime_error(
					"Cannot load plugin module " + normalized.string() + ": " +
					module->errorString().toStdString());
			QLibrary* result = module.get();
			session.m_Impl->modules.push_back(std::move(module));
			loadedModules.emplace(normalized, result);
			return *result;
		};

		for (const Descriptor& original : selected)
		{
			const Descriptor descriptor = PrepareDescriptor(original, shadowRoot, shadowCopy);
			if (!descriptor.runtime.empty())
			{
				QLibrary&  module = loadModule(descriptor.directory / descriptor.runtime);
				const auto create = reinterpret_cast<assetlib::CreateAssetPlugin>(
					module.resolve(assetlib::c_AssetPluginEntryPoint.data()));
				if (create == nullptr)
					throw std::runtime_error(
						"Runtime plugin entry point is missing: " + descriptor.id);
				assetlib::AssetPluginPtr plugin(create());
				if (plugin == nullptr)
					throw std::runtime_error(
						"Runtime plugin factory returned null: " + descriptor.id);

				assetlib::AssetKindRegistry staged;
				plugin->RegisterKinds(staged);
				session.m_Impl->kinds->Merge(std::move(staged));
				session.m_Impl->assetPlugins.push_back(std::move(plugin));
			}

			if (!descriptor.editor.empty())
			{
				QLibrary&  module = loadModule(descriptor.directory / descriptor.editor);
				const auto create = reinterpret_cast<editor::CreateEditorPlugin>(
					module.resolve(editor::c_EditorPluginEntryPoint.data()));
				if (create == nullptr)
					throw std::runtime_error(
						"Editor plugin entry point is missing: " + descriptor.id);
				editor::EditorPluginPtr plugin(create());
				if (plugin == nullptr)
					throw std::runtime_error(
						"Editor plugin factory returned null: " + descriptor.id);
				session.m_Impl->editorPlugins.push_back(std::move(plugin));
			}
			session.m_Impl->ids.push_back(descriptor.id);
		}

		return session;
	}

	bool
	OpeningNeedsPluginRelaunch(
		std::span<const std::string> loaded,
		std::span<const std::string> requested)
	{
		return !std::ranges::equal(loaded, requested);
	}
}
