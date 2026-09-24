#include "Plugins/plugin_loader.h"
#include "Plugins/EditorRegistry.h"
#include "util/editor_language.h"
#include <plugin_build_config.h>

#include <QCoreApplication>
#include <QLibrary>
#include <QString>
#include <QtAssert>
#include <assetlib/Project.h>

#include <algorithm>
#include <assetlib/AssetKindRegistry.h>
#include <assetlib/IAssetPlugin.h>
#include <core/platform/util.h>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/PluginDescriptor.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::plugins
{
	namespace
	{
		struct Descriptor
		{
			std::string                        id;
			std::string                        name;
			std::string                        description;
			std::string                        engineBuildId;
			std::string                        configuration;
			std::filesystem::path              directory;
			std::filesystem::path              runtime;
			std::filesystem::path              editor;
			std::vector<std::filesystem::path> dependencies;
		};

		bool
		ValidPluginId(const std::string_view id)
		{
			bool       componentStart = true;
			bool       sawDot         = false;
			const bool valid          = std::ranges::all_of(id, [&](const char c) {
				if (componentStart)
				{
					if (c < 'a' || c > 'z')
						return false;
					componentStart = false;
					return true;
				}
				if (c == '.')
				{
					componentStart = true;
					sawDot         = true;
					return true;
				}
				return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			});
			return valid && sawDot && !componentStart;
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
				throw editor::LocalizedError(
					"editor.plugins.descriptor_missing",
					{ file.string() },
					"Plugin descriptor is missing: {0}");

			try
			{
				const nlohmann::json json = nlohmann::json::parse(stream);
				if (json.at("version").get<uint32_t>() != editor::c_PluginDescriptorVersion)
					throw editor::LocalizedError(
						"editor.plugins.descriptor_version_unsupported",
						{ file.string() },
						"Plugin descriptor version is unsupported: {0}");

				Descriptor descriptor;
				descriptor.id            = json.at("id").get<std::string>();
				descriptor.name          = json.value("name", descriptor.id);
				descriptor.description   = json.value("description", std::string());
				descriptor.engineBuildId = json.at("engineBuildId").get<std::string>();
				descriptor.configuration = json.at("configuration").get<std::string>();
				descriptor.directory     = directory;
				descriptor.runtime       = json.value("runtime", std::filesystem::path());
				descriptor.editor        = json.value("editor", std::filesystem::path());
				descriptor.dependencies =
					json.value("dependencies", std::vector<std::filesystem::path>());

				if (!ValidPluginId(descriptor.id) ||
				    (descriptor.runtime.empty() && descriptor.editor.empty()))
					throw editor::LocalizedError(
						"editor.plugins.descriptor_incomplete",
						{ file.string() },
						"Plugin descriptor is incomplete: {0}");

				for (const std::filesystem::path& path : descriptor.dependencies)
					if (!IsContainedRelativePath(path))
						throw editor::LocalizedError(
							"editor.plugins.dependency_path_escapes",
							{ path.string() },
							"Plugin dependency path escapes its directory: {0}");
				for (const std::filesystem::path& path : { descriptor.runtime, descriptor.editor })
					if (!path.empty() && !IsContainedRelativePath(path))
						throw editor::LocalizedError(
							"editor.plugins.module_path_escapes",
							{ path.string() },
							"Plugin module path escapes its directory: {0}");

				return descriptor;
			}
			catch (const nlohmann::json::exception& error)
			{
				throw editor::LocalizedError(
					"editor.plugins.descriptor_malformed",
					{ file.string(), error.what() },
					"Malformed plugin descriptor {0}: {1}");
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
				throw editor::LocalizedError(
					"editor.plugins.wrong_engine",
					{ descriptor.id, descriptor.engineBuildId, build.id },
					"Plugin {0} was built for engine {1}, not {2}");
			if (descriptor.configuration != build.configuration)
				throw editor::LocalizedError(
					"editor.plugins.wrong_configuration",
					{ descriptor.id, descriptor.configuration, build.configuration },
					"Plugin {0} was built as {1}, not {2}");

			std::error_code ec;
			const auto      engineWrite = std::filesystem::last_write_time(build.sdkStamp, ec);
			if (ec)
				throw editor::LocalizedError(
					"editor.plugins.sdk_stamp_missing",
					{ build.sdkStamp.string() },
					"Editor SDK stamp is missing: {0}");

			for (const std::filesystem::path& relative : FilesOf(descriptor))
			{
				const std::filesystem::path file = descriptor.directory / relative;
				if (!std::filesystem::is_regular_file(file, ec) || ec)
					throw editor::LocalizedError(
						"editor.plugins.dependency_missing",
						{ file.string() },
						"Plugin dependency is missing: {0}");
				const auto pluginWrite = std::filesystem::last_write_time(file, ec);
				if (ec || pluginWrite < engineWrite)
					throw editor::LocalizedError(
						"editor.plugins.plugin_older_than_sdk",
						{ descriptor.id, file.string() },
						"Plugin {0} is older than this editor SDK: {1}");
			}
		}

		bool
		ShouldCopyPluginBinaries(const PluginBinaryCopyMode choice)
		{
			if (choice == PluginBinaryCopyMode::kAlways)
				return true;
			if (choice == PluginBinaryCopyMode::kNever)
				return false;
#if defined(_WIN32)
			return true;
#else
			return false;
#endif
		}

		Descriptor
		PreparePluginBinaries(
			const Descriptor&            descriptor,
			const std::filesystem::path& pluginCopyRoot,
			PluginBinaryCopyMode         copyMode)
		{
			if (!ShouldCopyPluginBinaries(copyMode))
				return descriptor;

			Descriptor prepared = descriptor;
			prepared.directory  = pluginCopyRoot / descriptor.id;
			std::error_code ec;
			std::filesystem::remove_all(prepared.directory, ec);
			std::filesystem::create_directories(prepared.directory, ec);
			if (ec)
				throw editor::LocalizedError(
					"editor.plugins.cannot_create_copy_directory",
					{ prepared.directory.string() },
					"Cannot create plugin copy directory: {0}");

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
					throw editor::LocalizedError(
						"editor.plugins.cannot_copy_file",
						{ destination.string() },
						"Cannot copy plugin file: {0}");
			}
			return prepared;
		}
	}

	struct PluginSession::Impl
	{
		std::vector<std::unique_ptr<QLibrary>>       modules;
		std::vector<assetlib::AssetPluginPtr>        assetPlugins;
		std::vector<editor::EditorPluginPtr>         editorPlugins;
		std::vector<std::filesystem::path>           editorLocalization;
		EditorRegistry                               contributions;
		std::shared_ptr<assetlib::AssetKindRegistry> kinds =
			std::make_shared<assetlib::AssetKindRegistry>();
		std::vector<std::string>  ids;
		std::vector<LoadedPlugin> plugins;
		bool                      editorRegistered = false;
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
	PluginSession::KindRegistry() const noexcept
	{
		return m_Impl->kinds;
	}

	std::span<const editor::EditorPluginPtr>
	PluginSession::EditorPlugins() const noexcept
	{
		return m_Impl->editorPlugins;
	}

	const EditorRegistry&
	PluginSession::Contributions() const noexcept
	{
		return m_Impl->contributions;
	}

	std::span<const LoadedPlugin>
	PluginSession::Plugins() const noexcept
	{
		return m_Impl->plugins;
	}

	BuildIdentity
	CurrentBuildIdentity()
	{
		return { std::string(c_EditorBuildId),
			     std::string(c_EditorBuildConfiguration),
			     std::filesystem::path(c_EditorSdkStamp) };
	}

	std::filesystem::path
	DefaultPluginCopyRoot()
	{
		return std::filesystem::temp_directory_path() / "bernini-editor-plugins" /
		       std::to_string(QCoreApplication::applicationPid());
	}

	std::filesystem::path
	DefaultPluginRoot()
	{
		return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
		       "plugins";
	}

	std::vector<std::filesystem::path>
	DiscoverPluginDirectories(const std::filesystem::path& root)
	{
		std::vector<std::filesystem::path> directories;
		std::error_code                    ec;
		for (const auto& entry : std::filesystem::directory_iterator(root, ec))
		{
			if (entry.is_directory(ec) && std::filesystem::is_regular_file(
											  entry.path() / editor::c_PluginDescriptorFileName,
											  ec))
				directories.push_back(entry.path());
		}
		std::ranges::sort(directories);
		return directories;
	}

	std::vector<std::filesystem::path>
	ConfiguredPluginDirectories(const std::filesystem::path& configPath)
	{
		std::ifstream stream(configPath);
		if (!stream)
			throw editor::LocalizedError(
				"editor.plugins.cannot_open_config",
				{ configPath.string() },
				"Cannot open editor config: {0}");
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
			throw editor::LocalizedError(
				"editor.plugins.malformed_config",
				{ error.what() },
				"Malformed editor config: {0}");
		}
	}

	PluginSession
	PluginSession::Load(
		std::span<const std::filesystem::path> directories,
		const BuildIdentity&                   build,
		const std::filesystem::path&           pluginCopyRoot,
		PluginBinaryCopyMode                   copyMode)
	{
		std::vector<Descriptor>         selected;
		std::unordered_set<std::string> seen;
		for (const std::filesystem::path& directory : directories)
		{
			Descriptor descriptor = ReadDescriptor(directory);
			if (!seen.emplace(descriptor.id).second)
				throw editor::LocalizedError(
					"editor.plugins.duplicate_directory",
					{ descriptor.id, directory.string() },
					"Plugin {0} is in more than one directory: {1}");
			ValidateDescriptor(descriptor, build);
			selected.push_back(std::move(descriptor));
		}

		PluginSession session;

		std::map<std::filesystem::path, QLibrary*> loadedModules;
		const auto loadModule = [&](const std::filesystem::path& path) -> QLibrary& {
			const std::filesystem::path normalized = path.lexically_normal();
			if (const auto found = loadedModules.find(normalized); found != loadedModules.end())
				return *found->second;

			auto module = std::make_unique<QLibrary>(QString::fromStdWString(normalized.wstring()));
			module->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
			if (!module->load())
				throw editor::LocalizedError(
					"editor.plugins.cannot_load_module",
					{ normalized.string(), module->errorString().toStdString() },
					"Cannot load plugin module {0}: {1}");
			QLibrary* result = module.get();
			session.m_Impl->modules.push_back(std::move(module));
			loadedModules.emplace(normalized, result);
			return *result;
		};

		for (const Descriptor& original : selected)
		{
			const Descriptor descriptor = PreparePluginBinaries(original, pluginCopyRoot, copyMode);
			LoadedPlugin     loaded;
			loaded.id          = descriptor.id;
			loaded.name        = descriptor.name;
			loaded.description = descriptor.description;
			loaded.directory   = original.directory;
			if (!descriptor.runtime.empty())
			{
				QLibrary&  module = loadModule(descriptor.directory / descriptor.runtime);
				const auto create = reinterpret_cast<assetlib::CreateAssetPlugin>(
					module.resolve(assetlib::c_AssetPluginEntryPoint.data()));
				if (create == nullptr)
					throw editor::LocalizedError(
						"editor.plugins.runtime_entry_point_missing",
						{ descriptor.id },
						"Runtime plugin entry point is missing: {0}");
				assetlib::AssetPluginPtr plugin(create());
				if (plugin == nullptr)
					throw editor::LocalizedError(
						"editor.plugins.runtime_factory_null",
						{ descriptor.id },
						"Runtime plugin factory returned null: {0}");

				assetlib::AssetKindRegistry staged;
				plugin->RegisterKinds(staged);
				session.m_Impl->kinds->Merge(std::move(staged));
				session.m_Impl->assetPlugins.push_back(std::move(plugin));
				loaded.runtimeModule = descriptor.directory / descriptor.runtime;
			}

			if (!descriptor.editor.empty())
			{
				QLibrary&  module = loadModule(descriptor.directory / descriptor.editor);
				const auto create = reinterpret_cast<editor::CreateEditorPlugin>(
					module.resolve(editor::c_EditorPluginEntryPoint.data()));
				if (create == nullptr)
					throw editor::LocalizedError(
						"editor.plugins.editor_entry_point_missing",
						{ descriptor.id },
						"Editor plugin entry point is missing: {0}");
				editor::EditorPluginPtr plugin(create());
				if (plugin == nullptr)
					throw editor::LocalizedError(
						"editor.plugins.editor_factory_null",
						{ descriptor.id },
						"Editor plugin factory returned null: {0}");
				session.m_Impl->editorPlugins.push_back(std::move(plugin));
				session.m_Impl->editorLocalization.push_back(original.directory / "localization");
				loaded.editorModule = descriptor.directory / descriptor.editor;
			}
			session.m_Impl->ids.push_back(descriptor.id);
			session.m_Impl->plugins.push_back(std::move(loaded));
		}

		return session;
	}

	void
	PluginSession::RegisterEditorPlugins(
		EditorPluginPtr              builtIn,
		const std::filesystem::path& builtInLocalization)
	{
		Q_ASSERT(!m_Impl->editorRegistered);
		m_Impl->editorRegistered = true;
		if (builtIn)
		{
			m_Impl->editorPlugins.insert(m_Impl->editorPlugins.begin(), std::move(builtIn));
			m_Impl->editorLocalization.insert(
				m_Impl->editorLocalization.begin(),
				builtInLocalization);
			m_Impl->plugins.insert(
				m_Impl->plugins.begin(),
				{ std::string(c_BuiltInPluginId),
			      editor::Localize("editor.plugins.builtin_plugin_name", "Bernini Editors")
			          .toStdString(),
			      editor::Localize(
					  "editor.plugins.builtin_plugin_description",
					  "The Material, Animation and Blend Space editors built into this editor.")
			          .toStdString() });
		}
		for (std::size_t i = 0; i < m_Impl->editorPlugins.size(); ++i)
			m_Impl->contributions.Register(
				*m_Impl->editorPlugins[i],
				ReadLocalizationDirectory(m_Impl->editorLocalization[i]));
	}

	assetlib::Project
	OpenProjectWithPlugins(const std::filesystem::path& projectFile, const PluginSession& session)
	{
		const std::vector<std::string> missing =
			MissingRequiredPlugins(session.Ids(), assetlib::Project::PluginIdsOf(projectFile));
		if (!missing.empty())
		{
			std::string ids;
			for (const std::string& id : missing) ids += (ids.empty() ? "" : ", ") + id;
			throw editor::LocalizedError(
				"editor.plugins.missing_required_plugins",
				{ projectFile.stem().string(), ids, DefaultPluginRoot().string() },
				"{0} requires plugins this editor did not load: {1}. Put each one in {2}/<id>/ "
				"or name its directory in pluginDirectories in config.json, then restart.");
		}
		return assetlib::Project::Open(projectFile, session.KindRegistry());
	}

	std::vector<std::string>
	MissingRequiredPlugins(
		const std::span<const std::string> loaded,
		const std::span<const std::string> required)
	{
		std::vector<std::string> missing;
		for (const std::string& id : required)
			if (std::ranges::find(loaded, id) == loaded.end())
				missing.push_back(id);
		return missing;
	}
}
